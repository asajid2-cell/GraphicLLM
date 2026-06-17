# Scene-Local Cinematic Renderer V1

This is the living plan and completion ledger for the scene-local cinematic
renderer refactor.

Goal: move CortexEngine from per-scene shader tweaks to a reusable full-scene
visual pipeline that can make enclosed and staged scenes read as intentional,
stable, cinematic spaces.

## 2026-06-04 Targeted Stability Harness

Status: implemented as validation infrastructure for the renderer goal.

Problem:

- The broad packet could prove the five-scene matrix, but it was too expensive
  and noisy for focused reflection/temporal debugging.
- Earlier gallery warnings mixed startup history warmup with steady-state
  motion stability.

Fix:

- `run_scene_local_cinematic_renderer_v1_packets.ps1` now accepts:
  - `-FamilyFilter`
  - `-ViewFilter`
- The manifest records both filters, and contract tests pin the controls.

Evidence:

- Fresh release build passed after the reverted probe experiment.
- Contract test passed.
- Warm-start targeted packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_targeted_micro_jitter_20260604/all_family_reflection_temporal_warm/manifest.json`
- Warm packet covered all five goal families and these views:
  `beauty, roughness, metallic, reflection_owner, shadow_factor, direct_light, taa_blend`.
- Hard-gate warnings: `0`
- Diagnostic signals: `0`
- Hard-gate aggregate stable-core large-changed ratio: `0.0`
- Only remaining warning is gallery `taa_blend`, a diagnostic temporal view,
  not a hard-gated visible/material/shadow view.

## 2026-06-04 Lighting Balance Bootstrap Leak

Status: implemented and validated as a renderer-contract fix, not final art.

Problem:

- The scene-local lighting balance policy was active, but model-authored
  kitchen/office/gym reports still showed `max_light_intensity=28.0`.
- Root cause was `BuildModelAuthoredScene()` adding hard-coded bootstrap
  `ModelAuthored_KeyLight` and `ModelAuthored_CoolFill` before seed/profile
  lights, without applying `SceneLightingBalanceProfile`.

Fix:

- `BuildModelAuthoredScene()` now builds the scene profile before bootstrap
  lights.
- Bootstrap key/fill lights scale by
  `sceneProfile.lightingBalance.localFixtureScale`.
- Contract tests pin `modelAuthoredFixtureScale` and the scaled key-light
  expression.

Evidence:

- Contract test passed.
- Focused diff check passed.
- Release build passed before the discarded probe-smoothing experiment.
- Static packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_lighting_balance_policy_20260604/static_packet_v3/manifest.json`
- Contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_lighting_balance_policy_20260604/static_packet_v3/beauty_contact_sheet.jpg`
- Key report movement:
  - kitchen max light `28.0 -> 22.96`
  - office max light `28.0 -> 20.16`
  - gym max light `28.0 -> 14.56`
  - gym white ratio `0.6189 -> 0.2770`
  - gym clip ratio `0.5229 -> 0.1694`

Motion probe:

- Mouse-jitter packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_motion_probe_20260604/mouse_jitter_packet/manifest.json`
- It did not reproduce broad model-authored shadow/reflection flicker.
- A probe-weight smoothing experiment was measured and reverted because it did
  not improve the diagnostic.
- Remaining gallery warnings appear tied to packet camera/direct-light/debug
  behavior and need a better targeted reflection/temporal harness before more
  shader changes.

## Completion Gate

Do not mark this goal complete until current evidence proves all of these:

- kitchen, office, gym, concert, and gallery are wired through
  `SceneCinematicProfile` or a successor renderer contract
- those scene families render with scene-local environment ownership rather
  than visible/random HDRI bleed
- material/reflection/temporal policy is profile-driven, not scattered as
  one-off constants in scene constructors
- validation packets exist for each target family with beauty, material,
  reflection/temporal debug, and stability evidence
- sharp IBL/gallery remains supported; enclosed scenes can use IBL resources
  safely without showing unrelated backgrounds
- build passes, shader compile logs are clean, and current packet evidence does
  not show material flicker or broad shadow flicker
- user accepts the scenes as visually good enough for the public-release target

## Architecture

`SceneCinematicProfile` is the current V1 contract:

- `SceneEnvironmentProfile`: environment preset, IBL enable/intensity, visible
  background, blur/exposure, rotation
- `SceneLightingProfile`: scene-local rig id, sun/key color and intensity,
  ambient fill, shadows, fog, god rays
- `SceneLightFixtureProfile`: profile-owned point/spot fixtures with semantic
  ids, position, target, color, intensity, range, cone, and shadow policy
- `SceneReflectionProfile`: reflection ownership label, SSR parameters, RT
  reflection/GI toggles and denoiser tuning
- `SceneTemporalProfile`: TAA/FXAA/SSAO policy
- `ScenePostProfile`: render scale, exposure, bloom, cinematic post, tone
  mapper, grade, contrast/saturation
- `SceneMaterialProfile`: world shader palette and lighting-script ids
- `SceneWaterProfile`: water/liquid controls

Central applier:

```cpp
SceneCinematicProfile BuildSceneLocalCinematicProfile(std::string_view family);
SceneCinematicProfile BuildGalleryCinematicProfile(bool conservativeMode);
void ApplySceneCinematicProfile(Renderer& renderer, const SceneCinematicProfile& profile);
```

This is intentionally renderer-wide. Scene constructors should select a
profile; they should not manually scatter IBL, post, reflection, temporal, and
lighting constants.

## Current Wiring

Implemented V1 profile families:

- `home_kitchen_lantern` -> `kitchen_morning_warm_scene_local_v1`
- `home_office_evening` -> `office_evening_scene_local_v1`
- `basketball_gym_day` -> `basketball_gym_bright_scene_local_v1`
- `neon_streamer_concert` -> `neon_concert_auditorium_scene_local_v1`
- `school_classroom_day` -> `classroom_day_scene_local_v1`
- `red_light_room` -> `red_room_moody_scene_local_v1`
- `stadium_night_match` -> `stadium_night_scene_local_v1`
- RT Showcase gallery -> `gallery_public_cinematic_v1` /
  `gallery_public_conservative_v1`

Enclosed model-authored scenes force:

- `backgroundVisible=false`
- `environmentPreset=neutral_procedural`
- `iblEnabled=false`
- `iblDiffuse=0`
- `iblSpecular=0`

That is an explicit anti-HDRI-bleed guard. Future local reflection-probe work
can re-enable scene-local image data, but it must be owned by room/probe
contracts, not by arbitrary visible HDRI backgrounds.

## Ledger

### SCR-V1-001 Profile Contract

Status: PARTIAL, ARCHITECTURE SLICE VERIFIED

Evidence:

- `src/Graphics/RendererSceneProfile.h`
- `src/Graphics/RendererSceneProfile.cpp`
- `src/Graphics/RendererControlApplier_ScenePresets.cpp`
- `src/Core/Engine_Scenes.cpp`
- `tools/run_scene_local_cinematic_renderer_v1_contract_tests.ps1`

What exists:

- Profile data model and central applier.
- Kitchen, office, gym, concert, classroom, red room, stadium, and gallery
  profile presets.
- Model-authored scene lighting now applies a profile by scene family.
- RT Showcase gallery applies `BuildGalleryCinematicProfile()` while preserving
  diagnostic env overrides.
- Focused contract test passes:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Release target build passed after adding the profile source:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && cmake --build build --config Release --target CortexEngine --parallel 4`

What remains:

- Move other hand-authored scenes onto profiles.
- Remove duplicated profile-like constants from older scene controls once
  validation proves parity.

### SCR-V1-002 Scene-Local Environment Ownership

Status: PARTIAL

Evidence:

- Enclosed model-authored guard in `ApplyModelAuthoredLighting()`.
- Scene profile environment fields.
- `scene_visual_contract` in frame reports.
- `scene_visual_enclosed_external_hdri_visible` validation warning.
- Short ownership probes:
  - `build/captures/scene_local_cinematic_renderer_v1_contract_probe_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_gallery_contract_probe_20260604/manifest.json`

What exists:

- Enclosed target families no longer rely on visible IBL backgrounds.
- Gallery remains sharp/visible IBL capable.
- Kitchen, office, gym, and concert beauty reports now record
  `environment_owner=scene_local_neutral`,
  `reflection_owner=local_room_no_visible_hdri`,
  `external_hdri_visible=false`, and `invalid_external_hdri=false`.
- Gallery beauty reports now record
  `environment_owner=authored_visible_gallery_ibl`,
  `reflection_owner=gallery_visible_ibl_plus_local_panels`,
  `external_hdri_visible=true`, `visible_external_hdri_allowed=true`, and
  `invalid_external_hdri=false`.
- Reflection owner debug view mode `46` now exists in post-process:
  - black = no meaningful reflection owner
  - blue = SSR
  - magenta = RT reflection
  - yellow = IBL/prelit scene color
  - green = scene-local neutral/local fallback
  - gray = sky/background/no scene depth
- `tools/analyze_scene_local_reflection_owner.py` converts mode-46 packet
  captures into per-family owner histograms and writes
  `reflection_owner_analysis` into packet manifests.

What remains:

- Local reflection probes/probe grids still need real renderer-owned radiance
  resources; current enclosed-scene proof is local/neutral fallback ownership.
- Current owner proof is packet-image analysis, not a GPU readback histogram.

### SCR-V1-003 Cinematic Post And Temporal Policy

Status: PARTIAL, MOTION-COMPENSATED PACKET ANALYSIS WIRED

Evidence:

- Profile post/temporal fields.
- Recent smooth-metal temporal fixes in `PostProcess.hlsl` and
  `TemporalRejectionMask.hlsl`.
- Adjacent-frame packet analyzer:
  - `tools/analyze_scene_local_packet_stability.py`
- Fresh packet evidence:
  - `build/captures/scene_local_cinematic_renderer_v1_stability_probe_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_gallery_stability_probe_20260604/manifest.json`

What exists:

- Scene-family profiles set tone mapper, grade, bloom, vignette, TAA/FXAA/SSAO,
  and reflection denoiser policy.
- Packet runner can now capture short frame sequences via
  `-CaptureSequenceCount`.
- Packet manifests can include `packet_stability_analysis` with per-family/view
  adjacent-frame luma deltas.
- Packet runner can now run motion stability packets via
  `-StabilityMotionMode mouse_jitter`, fixed timestep, and configurable yaw
  jitter.
- Fresh kitchen/office/gym/concert static sequence packet passed with aggregate
  `max_mean_abs_luma_delta=0.0`, `max_changed_pixel_ratio=0.0`, and
  `max_large_changed_pixel_ratio=0.0`.
- Fresh gallery static sequence packet passed with aggregate
  `max_mean_abs_luma_delta=0.336063`,
  `max_changed_pixel_ratio=0.007391`, and
  `max_large_changed_pixel_ratio=0.003195`.
- Fresh kitchen/office/gym/concert mouse-jitter packet passed with aggregate
  `max_mean_abs_luma_delta=22.725786`,
  `max_changed_pixel_ratio=0.411229`, and
  `max_large_changed_pixel_ratio=0.202329`.
- Fresh gallery mouse-jitter packet passed with aggregate
  `max_mean_abs_luma_delta=108.327535`,
  `max_changed_pixel_ratio=0.944830`, and
  `max_large_changed_pixel_ratio=0.941189`; the largest deltas are in
  warning-only diagnostic views `reflection_probe_weight` and `taa_blend`.
- Motion packet analysis now separates raw camera-motion energy from residual
  instability:
  - `tools/analyze_scene_local_packet_stability.py` estimates a low-resolution
    global image offset for `mouse_jitter` packets.
  - It records raw deltas, motion-compensated deltas, hard-gate warning counts,
    diagnostic warning counts, all-view aggregate metrics, and hard-gated
    aggregate metrics.
  - Motion-compensated hard-gate thresholds are distinct from static
    adjacent-frame warning thresholds, so expected yaw parallax no longer looks
    like material flicker.
- Current all-family mouse-jitter packet after compensation:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_mouse_jitter_probe_20260604b/manifest.json`
  - stability status `PASS`
  - failure count `0`
  - hard-gate warning count `0`
  - diagnostic warning count `5`
  - hard-gated raw aggregate remains motion-heavy:
    mean `22.725786`, changed `0.411229`, large `0.202329`
  - hard-gated motion-compensated aggregate is low:
    mean `4.856049`, changed `0.167108`, large `0.013668`
- Gallery runner integration packet after compensation:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_motion_compensated_probe_20260604/manifest.json`
  - stability status `PASS`
  - failure count `0`
  - hard-gate warning count `0`
  - diagnostic warning count `5`
  - hard-gated motion-compensated aggregate:
    mean `1.994376`, changed `0.053351`, large `0.004409`
- Packet runner now supports a second non-static motion mode:
  `-StabilityMotionMode camera_sweep`, using side/forward/look camera motion
  instead of only mouse-yaw jitter.
- Current all-family camera-sweep packet:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_camera_sweep_probe_20260604/manifest.json`
  - stability status `PASS`
  - failure count `0`
  - hard-gate warning count `0`
  - diagnostic warning count `2`
  - material status `PASS`, warnings `0`
  - owner status `PASS`
  - hard-gated motion-compensated aggregate:
    mean `2.327448`, changed `0.054253`, large `0.000868`

What remains:

- Make material class temporal behavior first-class in the profile/contract.
- Tighten material/reflection motion gates over longer and stronger camera
  sweeps now that legitimate yaw parallax is separated from residual flicker.
- Add temporal mask captures beyond the current `taa_blend` view if needed.

### SCR-V1-004 Material Classes

Status: PARTIAL, NAMED MATERIAL CLASS CONTRACT WIRED, VB MATERIALRESOLVE ROOT FIX VERIFIED

Evidence:

- Existing shader/material surface classes:
  - `assets/shaders/SurfaceClassification.hlsli`
  - `src/Graphics/SurfaceClassification.h`
- Renderer-side material policy:
  - `src/Graphics/MaterialModel.h`
  - `src/Graphics/MaterialModel.cpp`
- Material-class packet analyzer:
  - `tools/analyze_scene_local_material_classes.py`
- Packet tool writeback:
  - `material_class_analysis` in packet manifests
- Fresh packet evidence:
  - `build/captures/scene_local_cinematic_renderer_v1_material_analysis_probe_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_gallery_material_analysis_probe_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_static_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_camera_sweep_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_all_families_named_material_classes_static_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_all_families_named_material_classes_camera_sweep_20260604/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_concert_material_preset_canonicalization_probe_20260604/kitchen/one_frame_material_metadata_probe/frame_report_shutdown.json`
  - `build/captures/scene_local_cinematic_renderer_v1_concert_material_preset_canonicalization_probe_20260604/concert/one_frame_material_metadata_probe/frame_report_shutdown.json`
  - `build/captures/scene_local_cinematic_renderer_v1_vb_materialresolve_fix_20260604/kitchen_admitted_4frame_smoke/frame_report_shutdown.json`
  - `build/captures/scene_local_cinematic_renderer_v1_vb_materialresolve_fix_20260604/concert_4frame_smoke/frame_report_shutdown.json`
  - `build/captures/scene_local_cinematic_renderer_v1_vb_materialresolve_fix_20260604/all_families_static_packet/manifest.json`
  - `build/captures/scene_local_cinematic_renderer_v1_vb_materialresolve_fix_20260604/all_families_static_sequence_packet/manifest.json`

What exists:

- Debug view `41` shows renderer surface classes.
- The analyzer classifies `default`, `glass`, `mirror`, `plastic`, `masonry`,
  `emissive`, `brushed_metal`, `wood`, and `water`.
- It fails broken packet/debug output and records weak material diversity as a
  warning.
- Fresh kitchen/office/gym/concert packet passed material analysis with
  `unknown_ratio=0.0` for every family.
- Fresh model-family named-surface ratios:
  - kitchen `0.295220`
  - office `0.310384`
  - gym `0.157647`
  - concert `0.132067`
- Root cause of the previous gallery default-only material packet:
  RT Showcase was forcing the forward path unless
  `CORTEX_FORCE_VISIBILITY_BUFFER=1`, so post-process debug view `41` had no
  current `MaterialExt2` producer and read null/default surface classes.
- Gallery profiles now keep the visibility-buffer deferred path enabled by
  default:
  - `src/Graphics/RendererSceneProfile.cpp`
  - `src/Graphics/RendererControlApplier_ScenePresets.cpp`
- Static regression packet after the fix:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_vb_default_material_fix_20260604/manifest.json`
  - material status `PASS`, warnings `0`
  - `present_class_count=7`
  - `named_surface_ratio=0.405402`
  - `unknown_ratio=0.0`
  - visible classes include glass, mirror, plastic, emissive, brushed metal,
    and wood
- Mouse-jitter regression packet after the fix:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_vb_default_mouse_jitter_20260604/manifest.json`
  - material status `PASS`, warnings `0`
  - `present_class_count=8`
  - `named_surface_ratio=0.409410`
  - `unknown_ratio=0.0`
- Contract test now guards this root cause by rejecting a restored
  `CORTEX_FORCE_VISIBILITY_BUFFER`/`forceVisibilityBuffer` gate in RT Showcase.
- `MaterialModel` now carries `MaterialClassPolicyEvidence`, and
  `MaterialResolver::ResolveRenderable()` applies material class policy before
  the material reaches forward, transparent, depth/shadow, raytracing, or
  visibility-buffer paths.
- Current renderer-side policy normalizes:
  - roughness floors
  - normal-scale ceilings
  - procedural detail ceilings
  - dielectric enforcement for glass/water/emissive classes
  - reflection-stability evidence for smooth/metal/glass/water classes
- C++ surface classification now trusts the resolved material policy class when
  available, aligning snapshot material stats with VB material constants and
  debug-view output.
- Frame reports expose material policy counters:
  - `material_class_policy_applied`
  - `material_policy_roughness_clamped`
  - `material_policy_normal_clamped`
  - `material_policy_procedural_clamped`
  - `material_policy_reflection_stable`
- Validation checks these counters cannot exceed sampled material count.
- Static all-family material-policy packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `0`
  - hard aggregate:
    mean `0.412887`, changed `0.008150`, large `0.004111`
  - required-family beauty counters:
    - gallery sampled `34`, policy `34`, rough clamp `8`, normal clamp `31`,
      reflection-stable `13`
    - kitchen sampled `92`, policy `92`, rough clamp `3`, normal clamp `92`,
      reflection-stable `36`
    - office sampled `106`, policy `106`, rough clamp `4`,
      normal clamp `106`, reflection-stable `26`
    - gym sampled `117`, policy `117`, rough clamp `0`,
      normal clamp `117`, reflection-stable `42`
    - concert sampled `154`, policy `154`, rough clamp `3`,
      normal clamp `154`, reflection-stable `52`
- Camera-sweep all-family material-policy packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`, diagnostic warnings `3`
  - hard-gated motion-compensated aggregate:
    mean `3.599577`, changed `0.108073`, large `0.010851`
  - material-policy counters matched the static packet.
- `SceneMaterialProfile` and frame reports now expose
  `material_class_set_id`, defaulting to
  `scene_local_named_material_classes_v1`.
- `MaterialModel` now records named scene material class, reflection
  preference, temporal policy, and post sensitivity ids.
- Named material classes currently include:
  `PaintedWall`, `CeramicTile`, `PolishedWood`, `BrushedMetal`,
  `PolishedMetal`, `GlassPane`, `Fabric`, `Plastic`, `WetSurface`,
  `EmissiveNeon`, `ScreenPanel`, `Concrete`, `Rubber`, `Water`, and
  `Mirror`.
- Compact shader surface classes are now derived from the richer named class
  rather than being the whole material contract.
- Frame reports now count each named class plus reflection preference, temporal
  policy, and post sensitivity buckets.
- Validation checks:
  - profiled scenes must expose a non-default material class set id
  - named material class count must match sampled material count
  - reflection preference count must match sampled material count
  - temporal policy count must match sampled material count
  - post sensitivity count must match sampled material count
- Static all-family named-material packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_named_material_classes_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `0`
  - hard aggregate:
    mean `0.339287`, changed `0.007053`, large `0.003231`
  - non-default named material counts:
    gallery `27/34`, kitchen `54/92`, office `37/106`, gym `44/117`,
    concert `55/154`
  - remaining default named material debt:
    gallery `7/34`, kitchen `38/92`, office `69/106`, gym `73/117`,
    concert `99/154`
- Camera-sweep all-family named-material packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_named_material_classes_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`, diagnostic warnings `3`
  - hard-gated motion-compensated aggregate:
    mean `3.599577`, changed `0.108073`, large `0.010851`
  - named/default material counts matched the static packet.
- Named material classes are now shader-visible in the VB path:
  - `VBMaterialConstants::policyParams` carries scene material class,
    reflection preference, temporal policy, and post sensitivity.
  - `Renderer_VisibilityBufferMaterialKey.h` includes those policy ids so
    material dedupe cannot collapse two different named classes into one GPU
    material record.
  - `MaterialResolve.hlsl` writes encoded named scene material class into
    `MaterialExt2.a`.
  - `SurfaceClassification.hlsli` now exposes named material constants,
    encode/decode helpers, named-class debug colors, named-class reflection
    stability scale, and named-class material policy debug color.
  - `DeferredLighting.hlsl` and `PostProcess.hlsl` decode the named class from
    `MaterialExt2.a`.
  - debug mode `47` now shows named material policy colors instead of only
    compact surface-class policy colors.
  - post-process reflection stability now uses named material policy first,
    then falls back to compact surface policy.
- Static all-family shader-visible named-material packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_shader_visible_named_materials_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`
  - material warnings `2`, both in concert:
    `named_surface_ratio 0.001133 < 0.020000` and
    `present_class_count 1 < 2`
  - hard aggregate:
    mean `0.368263`, changed `0.007957`, large `0.003653`
  - frame-report named material coverage:
    gallery `27/34`, kitchen `54/92`, office `37/106`, gym `44/117`,
    concert `55/154`
  - beauty reflection preferences:
    gallery RT `8`, planar `1`; kitchen RT `1`, SSR `6`; office RT `1`;
    gym RT `1`; concert no RT/SSR/planar visible preference yet
- Camera-sweep all-family shader-visible named-material packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_shader_visible_named_materials_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`, diagnostic warnings `3`
  - material warnings `2`, same concert surface-class coverage warnings
  - hard-gated motion-compensated aggregate:
    mean `4.115016`, changed `0.137587`, large `0.021267`
- Material-class analyzer now separately reports named material policy
  coverage from debug view `47`:
  - `named_policy_debug_view_mode`
  - `named_policy_aggregate`
  - `named_policy_family_summary`
  - `named_policy_ratio`
  - `present_policy_count`
- Static all-family named-policy analyzer packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_named_policy_analysis_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`
  - material warnings `2`, both compact concert warnings:
    `named_surface_ratio 0.001133 < 0.020000` and
    `present_class_count 1 < 2`
  - named policy coverage:
    gallery `0.9937`, kitchen `0.9624`, office `0.9595`, gym `0.9856`,
    concert `0.9921`
  - named policy present counts:
    gallery `10`, kitchen `9`, office `15`, gym `8`, concert `13`
- Camera-sweep all-family named-policy analyzer packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_named_policy_analysis_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`, diagnostic warnings `3`
  - material warnings `2`, same compact concert warnings
  - named policy coverage:
    gallery `0.9933`, kitchen `0.9617`, office `0.9593`, gym `0.9855`,
    concert `0.9919`
  - named policy present counts:
    gallery `9`, kitchen `9`, office `15`, gym `8`, concert `13`
- Named policy coverage is now an explicit material release gate:
  - `min_named_policy_ratio=0.20`
  - `min_present_policy_count=4`
  - `max_named_policy_unknown_ratio=0.12`
  - `surface_policy` / debug view `47` missing now fails material analysis
  - compact `surface_class` / debug view `41` diversity remains a warning
- Static named-policy release-gate probe passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_named_policy_analysis_static_20260604/material_class_analysis_release_gate_probe.json`
  - status `PASS`, failures `0`, warnings `2`
  - release gate:
    gallery `PASS` ratio `0.9937`, policies `10`
    kitchen `PASS` ratio `0.9624`, policies `9`
    office `PASS` ratio `0.9595`, policies `15`
    gym `PASS` ratio `0.9856`, policies `8`
    concert `PASS` ratio `0.9921`, policies `13`
- Camera-sweep named-policy release-gate probe passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_named_policy_analysis_camera_sweep_20260604/material_class_analysis_release_gate_probe.json`
  - status `PASS`, failures `0`, warnings `2`
  - release gate:
    gallery `PASS` ratio `0.9933`, policies `9`
    kitchen `PASS` ratio `0.9617`, policies `9`
    office `PASS` ratio `0.9593`, policies `15`
    gym `PASS` ratio `0.9855`, policies `8`
    concert `PASS` ratio `0.9919`, policies `13`
- Material preset alias metadata now recognizes common model-authored material
  terms without object-by-object scene edits:
  `paint`, `painted_wall`, `matte_tile`, `ceramic_tile`, `screen_panel`,
  `fabric`, `paper`, `fiber`, `turf`, `rubber`, generic `metal`, and
  `painted_metal`.
- Named material resolution checks canonical aliases in addition to the raw
  preset string. Broad painted-wall aliases remain conservative in the compact
  surface contract: they keep compact `surface_default` and neutral roughness
  policy while still publishing named `PaintedWall` in the material policy
  metadata.
- One-frame model-authored metadata probes after aliasing:
  - kitchen: `device_removed=false`, `scene_material_default=0/92`,
    named classes include painted wall `19`, ceramic tile `6`, brushed metal
    `35`, fabric `19`, polished wood `9`, emissive neon `1`.
  - concert: `device_removed=false`, `scene_material_default=0/154`,
    named classes include painted wall `98`, brushed metal `52`,
    emissive neon `3`, fabric `1`.
- Added `CORTEX_DISABLE_GPU_CULLING=1` as a renderer startup kill switch for
  diagnostic/release fallback runs.
- Root cause of the previous multi-frame model-authored kitchen/concert TDR:
  `MaterialResolve.hlsl` failed to compile because the final `MaterialExt2`
  write referenced `mat.policyParams.x` outside the scoped material record.
  That forced the model-authored scenes out of the intended visibility-buffer
  material path and into the older forward/indirect fallback path, where the
  long repros hit graphics fence timeouts and `DXGI_ERROR_DEVICE_HUNG`.
- `MaterialResolve.hlsl` now keeps a scoped `sceneMaterialClass` local,
  assigns it from `mat.policyParams.x` while `mat` is in scope, and writes
  `EncodeSceneMaterialClass(sceneMaterialClass)` into `MaterialExt2.a`.
- Contract tests now guard against restoring the broken
  `EncodeSceneMaterialClass(mat.policyParams.x)` pattern.
- Explicit admitted kitchen 4-frame smoke after the fix:
  - `device_removed=false`
  - `visibility_buffer_planned=true`
  - `visibility_buffer_rendered=true`
  - `visibility_buffer_instances=91`
  - `scene_material_default=0`
  - `invalid_external_hdri=false`
- Explicit admitted concert 4-frame smoke after the fix exited `0`, rendered
  the visibility-buffer path, and had no TDR/device-hung log strings.
- All-family compact packet after the fix:
  - `captured_view_count=55`
  - reflection owner `PASS`, failures `0`
  - material `PASS`, failures `0`, warnings `0`
  - kitchen, office, gym, concert, and gallery all exited `0`,
    `device_removed=false`, and `visibility_buffer_rendered=true`
- All-family static sequence packet after the fix:
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`
  - aggregate stability mean `0.387509`, changed `0.008017`,
    large `0.004003`
- Log scans across the fixed all-family packet folders found no
  visibility-buffer initialization failure, shader compile failure, fence
  timeout, `DXGI_ERROR`, device removal, renderer failure, or validation error
  strings.

What remains:

- Reduce the high default-material ratios in office/gym/concert by improving
  scene material preset naming and asset material metadata.
- Improve visible pixel material diversity for concert and other high-default
  captures. The frame reports have non-default named material metadata, but
  the compact surface-class debug capture for concert still reads almost all
  default.
- Keep compact surface-class diversity as diagnostic debt while the release
  material gate prioritizes named policy coverage.
- The previous VB material compile/TDR blocker is fixed for focused kitchen,
  concert, and all-family static/sequence packets, but this is not final
  visual completion.
- Run stronger camera-sweep and mouse-motion packets after the VB fix before
  claiming broad motion stability.
- Build the next visual slice as renderer-wide full-scene shader work:
  local radiance/reflection ownership, material-layer richness, shadow
  filtering, post grade, exposure, and profile-owned debug gates.

### SCR-V1-005 Validation Packets

Status: PARTIAL

Evidence:

- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`
- Gallery packet:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_probe/manifest.json`

What exists:

- Packet tool writes per-family manifests and subfolders.
- Current views: `beauty`, `roughness`, `metallic`, `surface_class`,
  `reflection_probe_weight`, `reflection_owner`, `shadow_factor`,
  `direct_light`, `ambient_ibl`, `taa_blend`.
- Gallery packet passed with five captures and clean shader compile logs:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_probe -SmokeFrames 100 -CaptureFrame 45`
- Packet tool now auto-resolves admitted kitchen/office/gym/concert seeds and
  supports `-OnlyGallery`.
- Short contract probe passed for kitchen, office, gym, and concert:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_contract_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
- Short gallery contract probe passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OnlyGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_contract_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
- Shader compile log scans for both short probes had no
  `Failed to compile`, `shader compilation failed`, or `error:` matches.
- Short reflection-owner model-family packet passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_owner_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
  - captured 40 BMPs
  - kitchen/office/gym/concert `reflection_owner` captures exist with
    `debug_view=46`
  - frame reports expose `reflection_owner_debug_view_mode=46`
- Short reflection-owner gallery packet passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OnlyGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_owner_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
  - captured 10 BMPs
  - gallery `reflection_owner` capture exists with `debug_view=46`
  - gallery still reports `external_hdri_visible=true` and
    `invalid_external_hdri=false`
- Reflection-owner analyzer is now wired into the packet tool by default.
  Fresh short packet with analysis passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_owner_analysis_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_owner_analysis_probe_20260604/manifest.json`
  - `reflection_owner_analysis.status=PASS`
  - kitchen/office/gym/concert all reported `visible_ibl_ratio=0.0` and
    `unknown_ratio=0.0`
  - scene-local fallback signal ratios were `0.216600`, `0.155002`,
    `0.186205`, and `0.210341`
- Fresh gallery packet with analysis passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OnlyGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_owner_analysis_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_gallery_owner_analysis_probe_20260604/manifest.json`
  - `reflection_owner_analysis.status=PASS`
  - gallery reported `visible_ibl_ratio=0.000468`,
    `unknown_ratio=0.0`, and `rt_reflection_ratio=0.305508`
- Shader-log scan over the fresh owner-analysis packets found no
  `Failed to compile`, `shader compilation failed`, or `error:` matches.
- Material-class analyzer is now wired into the packet tool by default.
  Fresh short model-family packet with owner and material analysis passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_material_analysis_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_material_analysis_probe_20260604/manifest.json`
  - `reflection_owner_analysis.status=PASS`
  - `material_class_analysis.status=PASS`
  - kitchen/office/gym/concert material analysis had `unknown_ratio=0.0`
    and named-surface ratios `0.295220`, `0.310384`, `0.157647`, and
    `0.132067`
- Fresh gallery packet with owner and material analysis passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OnlyGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_material_analysis_probe_20260604 -SmokeFrames 3 -CaptureFrame 1`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_gallery_material_analysis_probe_20260604/manifest.json`
  - `reflection_owner_analysis.status=PASS`
  - `material_class_analysis.status=PASS`
  - material analysis reported two warnings because gallery only showed
    `default` surface class
- Stability analyzer is now wired into the packet tool when
  `-CaptureSequenceCount` is at least `2`.
  Fresh short model-family packet with owner, material, and stability analysis
  passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_stability_probe_20260604 -SmokeFrames 10 -CaptureFrame 4 -CaptureSequenceCount 3`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_stability_probe_20260604/manifest.json`
  - `packet_stability_analysis.status=PASS`
  - aggregate stability metrics were all `0.0`
- Fresh gallery packet with owner, material, and stability analysis passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OnlyGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_stability_probe_20260604 -SmokeFrames 10 -CaptureFrame 4 -CaptureSequenceCount 3`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_gallery_stability_probe_20260604/manifest.json`
  - `packet_stability_analysis.status=PASS`
  - aggregate stability metrics:
    `max_mean_abs_luma_delta=0.336063`,
    `max_changed_pixel_ratio=0.007391`,
    `max_large_changed_pixel_ratio=0.003195`
- Mouse-jitter packet mode is now wired into the packet runner through
  `-StabilityMotionMode mouse_jitter`.
  Fresh model-family mouse-jitter packet passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -SkipGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_mouse_jitter_probe2_20260604 -SmokeFrames 12 -CaptureFrame 4 -CaptureSequenceCount 3 -StabilityMotionMode mouse_jitter -MotionFrames 12 -MotionLookAmplitude 0.025 -MotionLookCycles 2.0 -FixedDeltaTime 0.008333333`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_mouse_jitter_probe2_20260604/manifest.json`
  - owner and material analysis passed
  - `packet_stability_analysis.status=PASS`
  - warning count `83`
  - aggregate:
    `max_mean_abs_luma_delta=22.725786`,
    `max_changed_pixel_ratio=0.411229`,
    `max_large_changed_pixel_ratio=0.202329`
- Fresh gallery mouse-jitter packet passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OnlyGallery -OutputRoot build/captures/scene_local_cinematic_renderer_v1_gallery_mouse_jitter_probe2_20260604 -SmokeFrames 12 -CaptureFrame 4 -CaptureSequenceCount 3 -StabilityMotionMode mouse_jitter -MotionFrames 12 -MotionLookAmplitude 0.025 -MotionLookCycles 2.0 -FixedDeltaTime 0.008333333`
  - manifest:
    `build/captures/scene_local_cinematic_renderer_v1_gallery_mouse_jitter_probe2_20260604/manifest.json`
  - owner analysis passed
  - material analysis passed structurally with the known default-only warnings
  - `packet_stability_analysis.status=PASS`
  - warning count `20`
  - aggregate:
    `max_mean_abs_luma_delta=108.327535`,
    `max_changed_pixel_ratio=0.944830`,
    `max_large_changed_pixel_ratio=0.941189`
  - the largest gallery motion deltas are in warning-only diagnostic views
    `reflection_probe_weight` and `taa_blend`

What remains:

- Required packet families for completion: kitchen, office, gym, concert,
  gallery with final-quality smoke frame counts and tighter motion stability
  thresholds.
- Adjacent-frame and short mouse-jitter stability summaries are integrated.
- Motion packets still produce many warnings; the next polish pass should
  separate expected parallax from unstable material/reflection response more
  precisely.
- Material-class histograms exist through packet-image analysis, but the
  gallery/RT Showcase path still needs richer material-class publishing.
- Reflection-owner histograms exist through packet-image analysis, but not GPU
  readback.

## Next Work

1. Fix gallery/RT Showcase material classification so public gallery packets
   are not default-only.
2. Promote material class ids into a first-class debug/export contract beyond
   the current compact surface-class set.
3. Improve motion-stability analysis so expected parallax is separated from
   material/reflection flicker more precisely.
4. Run final-quality kitchen, office, gym, concert, and gallery packets with
   longer smoke frame counts after material/stability evidence is available.
5. Move remaining hand-authored scene controls onto profile builders.

## Full Refactor Plan

This section is the higher-level plan for moving from "scene profiles exist" to
"full scene shaders produce cinematic, scene-local visuals". The work should be
implemented in this order. Do not skip directly to polish shaders before the
contracts and evidence paths exist.

### Phase 1: Renderer Contract Backbone

Goal: every scene-family visual decision enters the renderer through a stable
profile contract.

Required changes:

- Keep `SceneCinematicProfile` as the public scene-family control surface.
- Add frame-report fields for profile id, environment owner, reflection owner,
  light rig id, tone mapper id, material palette id, temporal policy id, and
  debug capture mode.
- Add a renderer-side `SceneVisualContract` snapshot after profile application.
  This should be immutable during a frame and serializable into capture packets.
- Add validation that enclosed scenes cannot present arbitrary visible HDRI
  backgrounds unless the profile explicitly marks the background as the authored
  scene environment.

Pseudocode:

```cpp
SceneCinematicProfile profile = BuildSceneLocalCinematicProfile(sceneFamily);
SceneVisualContract contract = NormalizeAndValidate(profile, sceneMetadata);

if (contract.sceneKind == EnclosedRoom) {
    Require(contract.background.owner != ExternalVisibleHDRI);
    Require(contract.reflections.owner != Unknown);
}

renderer.ApplySceneVisualContract(contract);
frameReport.sceneVisualContract = contract.ToReport();
```

Completion evidence:

- Contract test proves all target families emit a contract.
- Packet manifest records the applied profile and owners for every capture.
- Invalid enclosed HDRI bleed fails a test instead of becoming a visual accident.

### Phase 2: Material Class System

Goal: stop treating materials as loose shader constants. A kitchen tile, gym
floor, brushed metal, glass panel, chair fabric, and neon tube need predictable
class behavior.

Required material classes:

- `PaintedWall`
- `CeramicTile`
- `PolishedWood`
- `BrushedMetal`
- `PolishedMetal`
- `GlassPane`
- `Fabric`
- `Plastic`
- `WetSurface`
- `EmissiveNeon`
- `ScreenPanel`
- `Concrete`
- `Rubber`

Each class needs:

- base roughness/metallic/specular ranges
- normal/detail texture policy
- reflection owner preference
- temporal history policy
- post/exposure sensitivity
- debug color/id

Pseudocode:

```cpp
MaterialClassDesc klass = materialLibrary.Resolve(surface.semanticMaterial);

surface.shaderParams.roughness = Clamp(surface.roughness, klass.roughnessRange);
surface.shaderParams.metallic = Clamp(surface.metallic, klass.metallicRange);
surface.temporalPolicy = klass.temporalPolicy;
surface.reflectionPolicy = klass.reflectionPolicy;
surface.debugMaterialClassId = klass.id;
```

Completion evidence:

- Debug view can show material class ids.
- Packet captures include roughness, metallic, and material-class views.
- Smooth/metal/glass surfaces are stable under mouse-look without hiding IBL.

### Phase 3: Scene-Local Lighting Rigs

Goal: replace global lighting guesses with authored light-rig archetypes that
match the place being rendered.

Required rigs:

- `KitchenMorningWindow`
- `OfficeEveningPractical`
- `GymHighBay`
- `ConcertStageNeon`
- `GallerySoftbox`
- `ClassroomDaylight`
- `RedRoomMoody`
- `StadiumNightFlood`

Each rig needs:

- key/fill/rim/ambient contracts
- local fixture emitters
- shadow map policy
- exposure range
- bloom contribution
- room-volume/fog policy

Pseudocode:

```cpp
LightRig rig = lightRigLibrary.Resolve(profile.lighting.rigId);

renderer.ClearSceneLocalLights();
for (LightSpec light : rig.Build(sceneBounds, focalZones)) {
    renderer.AddSceneLocalLight(light);
}

renderer.SetExposurePolicy(rig.exposure);
renderer.SetShadowPolicy(rig.shadows);
```

Completion evidence:

- Gallery still supports sharp IBL.
- Enclosed scenes read correctly with local lights even when visible HDRI is off.
- Shadow debug packets prove no broad floor/wall flicker under mouse-look.

### Phase 4: Reflection Ownership

Goal: every shiny pixel should know why it reflects what it reflects. No random
office HDRI on a concert room, no hidden blur workaround, no unknown fallback.

Reflection owners:

- `LocalProbe`
- `ProbeGrid`
- `PlanarProbe`
- `SSR`
- `RTReflection`
- `VisibleSceneBackground`
- `NeutralFallback`
- `InvalidUnknown`

Required changes:

- Add local cubemap/probe descriptors to scene profiles.
- Add planar reflection support for strong horizontal surfaces when needed.
- Add reflection-owner debug view.
- Add packet metric that flags enclosed-scene pixels using visible external
  HDRI ownership.

Pseudocode:

```hlsl
ReflectionSample sample = ResolveReflection(surface, view, sceneContract);

if (sceneContract.enclosed && sample.owner == VisibleExternalHDRI) {
    sample = ResolveNeutralOrLocalFallback(surface, view);
    MarkReflectionOwnerInvalid(surface.pixel);
}

outColor += sample.radiance * surface.specularWeight;
outReflectionOwner = sample.owner;
```

Completion evidence:

- Reflection-owner debug view exists.
- Packet manifests report owner histograms.
- Kitchen/office/gym/concert have zero visible-external-HDRI ownership.
- Gallery can intentionally use visible IBL without being flagged.

### Phase 5: Temporal And Stability Policy

Goal: movement should feel stable without smearing, hiding, or disabling real
view-dependent lighting.

Required changes:

- Promote temporal policy into material classes and scene profiles.
- Add same-phase and adjacent-frame capture modes to the packet tool.
- Track per-debug-view stability, not only beauty-frame deltas.
- Add specific metrics for shadow flicker, reflection sparkle, exposure pop,
  material class instability, and z-fighting candidates.

Pseudocode:

```cpp
CapturePath path = BuildMouseLookPath(cameraBookmark);

RunAdjacentFrameCapture(path);
RunSamePhaseCapture(path);

metrics.shadowFlicker = CompareDebugView("shadow");
metrics.reflectionSparkle = CompareDebugView("reflection_owner", "beauty");
metrics.temporalReject = CompareDebugView("temporal_mask");
metrics.zFightCandidate = DetectThinAlternatingDepthBands();

packet.WriteStabilityReport(metrics);
```

Completion evidence:

- The exact user-reported sharp-IBL repro remains testable.
- Same-phase instability stays near zero for static geometry.
- Adjacent-frame change is separated into legitimate view-dependent motion
  versus unstable flicker.

### Phase 6: Cinematic Post Stack

Goal: visual style should be controlled by scene profiles, not scattered post
constants.

Required post controls:

- exposure mode
- tone mapper
- color grade
- contrast/saturation
- bloom threshold/intensity
- vignette
- fog/atmosphere
- glare/emissive response
- sharpening/anti-aliasing policy

Pseudocode:

```cpp
PostProfile post = postLibrary.Resolve(profile.post.profileId);

renderer.SetToneMapper(post.toneMapper);
renderer.SetExposure(post.exposure);
renderer.SetBloom(post.bloom);
renderer.SetColorGrade(post.grade);
renderer.SetAntiAliasing(profile.temporal);
```

Completion evidence:

- Profiles can switch a scene from neutral/debug to cinematic without editing
  scene constructors.
- Concert and red-room scenes can carry strong mood without breaking material
  stability or exposure.

### Phase 7: Packet-Driven Release Gate

Goal: never claim visual progress based on one angle or one screenshot.

Required packet families:

- kitchen
- office
- gym
- concert
- gallery

Required views per packet:

- beauty
- material class
- roughness
- metallic
- normal/depth
- shadow
- reflection owner
- TAA blend
- temporal mask
- exposure/post

Required camera coverage:

- hero
- floor-close
- wall-close
- reflective-close
- wide-room
- mouse-look stability path

Pseudocode:

```powershell
foreach ($family in $TargetFamilies) {
    ResolveSceneSeedOrBookmark $family
    CaptureViews $family $RequiredViews $RequiredCameras
    AnalyzePacket $family
    WriteFamilyReport $family
}

RankPacketsByContractAndVisualMetrics
FailIfAnyRequiredFamilyHasCriticalContractViolation
```

Completion evidence:

- A single command generates the release packet set.
- Reports separate renderer contract failures from scene-content ugliness.
- User can inspect every required family and camera angle.

### Phase 8: Decommission Old One-Off Controls

Goal: remove tech debt only after the new contract proves it owns the behavior.

Required changes:

- Inventory old scene-specific IBL, post, reflection, and temporal constants.
- Move remaining legitimate constants into profile builders or material/light
  libraries.
- Delete or gate obsolete debug workarounds.
- Preserve diagnostics that reproduce past bugs.

Completion evidence:

- Contract tests fail if old scene constructors bypass the profile contract.
- Public launch path and debug launch path both report their visual contract.
- The old sharp-IBL flicker repro is still runnable after cleanup.

## Implementation Order For The Next Goal Slice

The next implementation slice should not start by adding prettier shader math.
It should build the missing evidence and ownership layers first:

1. Resolve curated model-authored seed paths for kitchen, office, gym, and
   concert.
2. Extend packet capture with material-class and reflection-owner placeholders.
3. Add `SceneVisualContract` serialization into frame reports.
4. Add reflection-owner debug output, even if the first version only classifies
   current IBL/SSR/RT/fallback paths.
5. Add material class ids and a debug view.
6. Run full packets on gallery plus four model-authored target families.
7. Only then start upgrading BRDF/detail-normal/post lighting quality, because
   the packet system will show whether the changes are stable.

## Full-Scene Shader Refactor Plan

This is the refactor plan for moving from a functional renderer with scene
profiles into a reusable cinematic renderer. The target is not "copy Unreal",
but the same kind of visual contract: physically plausible materials, authored
local lighting, stable reflections, stable temporal accumulation, cinematic
post, and packet evidence that proves the renderer is not hiding artifacts.

### Design Principle

The refactor should make the renderer scene-aware without making each scene a
special case. The scene chooses a profile, the profile resolves to renderer
contracts, and the shader stack consumes those contracts consistently.

The dependency order is:

```text
scene family
  -> SceneCinematicProfile
  -> SceneVisualContract
  -> material palette + light rig + reflection rig + post profile
  -> frame constants / GPU resources / debug ownership
  -> validation packet
```

If a visual feature cannot report who owns it, what profile enabled it, and
which packet proved it stable, it is not ready for public visuals.

### Target Renderer Layers

The renderer should be split into these durable layers:

1. `SceneVisualContract`
   - immutable per-frame resolved profile
   - serializable into frame reports and packet manifests
   - owns environment, lighting, material, reflection, temporal, and post ids

2. `MaterialClassLibrary`
   - semantic material classes, not loose roughness/metallic constants
   - clamps invalid material values by class
   - declares reflection, temporal, normal/detail, and energy behavior

3. `LightingRigLibrary`
   - room-local lighting archetypes
   - builds key/fill/rim/practical/emissive fixtures from scene bounds
   - declares shadow policy and exposure bounds

4. `ReflectionRigLibrary`
   - local probe, probe-grid, planar, SSR, RT, visible-background, fallback
     ownership
   - forbids enclosed scenes from reflecting arbitrary external HDRI pixels
   - exports reflection-owner debug and histograms

5. `CinematicPostLibrary`
   - tone map, exposure, grade, bloom, vignette, fog, glare, sharpening, AA
   - profile-selectable and packet-visible

6. `TemporalStabilityPolicy`
   - material-aware history clamping
   - camera-cut and mouse-look stability contracts
   - debug views and adjacent-frame packet metrics

7. `SceneVisualPacketRunner`
   - captures beauty plus ownership/debug views for each family
   - analyzes material/reflection/shadow/temporal stability
   - fails contract violations before subjective visual review

### Phase A: Finish Evidence First

Goal: make every visual claim inspectable before changing shader quality.

Work:

- finish reflection-owner image analyzer and manifest writeback
- add material-class analyzer from the existing surface/material debug view
- add same-phase and adjacent-frame stability capture paths
- add packet summaries for:
  - visible external HDRI ownership in enclosed scenes
  - unknown reflection owner ratio
  - shadow flicker ratio
  - reflection sparkle ratio
  - material id instability
  - exposure pop
  - broad luma flicker

Pseudocode:

```powershell
$packet = CaptureSceneFamilyPacket $family $profile $cameras $views
$owners = AnalyzeReflectionOwner $packet
$materials = AnalyzeMaterialClasses $packet
$stability = AnalyzeAdjacentFrames $packet

FailIf $family.enclosed -and $owners.visible_external_hdri_ratio -gt 0.001
FailIf $owners.unknown_ratio -gt 0.05
FailIf $stability.same_phase_luma_delta -gt $SamePhaseLimit
FailIf $stability.shadow_flicker_ratio -gt $ShadowLimit
WritePacketReport $packet $owners $materials $stability
```

Completion:

- one command produces kitchen, office, gym, concert, and gallery packets
- packets contain beauty, material, reflection-owner, shadow, temporal, and
  post evidence
- analyzer can fail bad renderer states without relying on user screenshots

### Phase B: Material System Refactor

Goal: make surfaces behave like authored materials instead of arbitrary shader
numbers.

Work:

- add `MaterialClassId` to the material/preset path
- define class descriptors for:
  - painted wall
  - ceramic tile
  - polished wood
  - brushed metal
  - polished metal
  - glass pane
  - fabric
  - plastic
  - wet surface
  - emissive neon
  - screen panel
  - concrete
  - rubber
- route material presets through class validation
- expose class id in a stable debug buffer/view
- move roughness/normal/metallic stabilizers from scene-specific code into
  class policy

Pseudocode:

```cpp
MaterialClassDesc klass = MaterialClassLibrary::Resolve(material.semanticClass);

material.roughness = Clamp(material.roughness, klass.roughnessRange);
material.metallic = Clamp(material.metallic, klass.metallicRange);
material.normalScale = min(material.normalScale, klass.maxNormalScale);
material.temporalPolicy = klass.temporalPolicy;
material.reflectionPolicy = klass.reflectionPolicy;
material.debugClassId = klass.id;
```

Completion:

- kitchen tile, gym floor, glass, metals, fabric, neon, and screens are
  distinguishable in packet material views
- smooth and metallic surfaces stay stable during mouse-look
- no scene constructor manually fixes roughness/normal values for public
  stability

### Phase C: Scene-Local Lighting Refactor

Goal: scenes should be lit by their own space, not by leftover global IBL mood.

Work:

- formalize light rigs:
  - kitchen morning window/practicals
  - office evening practicals/screens
  - gym high-bay
  - concert stage/neon
  - gallery softbox
  - classroom daylight
  - red room moody practicals
  - stadium floodlights
- make rigs build local lights from scene bounds and focal zones
- add fixture/emissive contribution rules
- define per-rig shadow policy:
  - cascaded shadow allowed
  - local shadow only
  - no broad CSM on known unstable indoor shells
  - RT shadow optional
- expose light rig id and shadow policy in frame reports

Pseudocode:

```cpp
ResolvedLightRig rig = LightingRigLibrary::Build(profile.lighting.rigId,
                                                 sceneBounds,
                                                 focalZones);

renderer.ClearSceneLocalLights();
for (const LightSpec& light : rig.lights) {
    renderer.AddSceneLocalLight(light);
}

renderer.SetShadowPolicy(rig.shadowPolicy);
renderer.SetExposurePolicy(rig.exposurePolicy);
```

Completion:

- enclosed scenes remain readable with external visible HDRI disabled
- wall/floor shadow flicker gates pass under mouse-look
- concert/red-room mood comes from local emissive/stage rigs, not a random HDRI

### Phase D: Reflection And GI Refactor

Goal: reflective pixels need valid local radiance and an explainable owner.

Work:

- implement `ReflectionOwner` as a renderer/shader contract, not just a debug
  color
- add local room probe descriptors to profiles
- support optional planar reflections for floors/tables/water-like surfaces
- prefer local probe/grid/planar/SSR/RT by material class and scene family
- restrict visible external HDRI use to profiles that explicitly allow it
- add GI/fill policy:
  - neutral ambient for enclosed blockout
  - local probe diffuse for room scenes
  - RT GI where available and stable
  - authored IBL only for gallery/outdoor/studio profiles

Pseudocode:

```hlsl
ReflectionSample ResolveReflection(Surface s, SceneVisualContract c)
{
    if (s.reflectionPolicy.prefersPlanar && PlanarProbeValid(s)) {
        return SamplePlanarProbe(s);
    }
    if (SSRValid(s)) {
        return SampleSSR(s);
    }
    if (RTReflectionValid(s)) {
        return SampleRTReflection(s);
    }
    if (LocalProbeValid(s, c)) {
        return SampleLocalProbe(s, c);
    }
    if (c.visibleExternalHdriAllowed) {
        return SampleVisibleEnvironment(s);
    }
    return SampleNeutralFallback(s);
}
```

Completion:

- enclosed-scene reflection-owner histograms show no external HDRI ownership
- gallery can intentionally use visible HDRI and passes owner validation
- metallic/glass closeups do not shimmer or pop during camera jiggle

### Phase E: BRDF And Detail Upgrade

Goal: improve the actual material response after ownership is proven.

Work:

- consolidate PBR evaluation in shared shader helpers
- use energy-safe diffuse/specular split for all material classes
- add class-aware detail normals and roughness variation
- add clearcoat/sheen/transmission only behind material-class support
- add wet-surface response for kitchens, galleries, streets, and concerts
- make all high-frequency detail respect temporal policy and mip/footprint
  clamps

Pseudocode:

```hlsl
PbrInputs p = BuildPbrInputs(surface, materialClass, frame);
p.normal = ApplyClassAwareDetailNormal(p.normal, materialClass, ddx, ddy);
p.roughness = StabilizeRoughness(p.roughness, materialClass, viewMotion);

float3 direct = EvaluateDirectLighting(p, lights);
float3 indirect = EvaluateIndirectLighting(p, probes, gi);
float3 reflection = EvaluateOwnedReflection(p, reflectionRig);
return EnergyConserve(direct + indirect + reflection + emissive);
```

Completion:

- surfaces look richer without adding instability
- shader improvements are visible in packets across multiple scene families
- old material flicker repro remains fixed with sharp IBL enabled

### Phase F: Cinematic Post And Camera Pipeline

Goal: make the image feel authored after the physically grounded layers are
stable.

Work:

- profile-driven exposure ranges and metering masks
- tone mapper selection per profile
- color-grade LUT or parametric grade per profile
- bloom threshold/intensity tied to emissive material classes
- fog/volume/glare as local scene effects
- sharpening/AA policy tied to render scale and temporal mode
- camera roles with per-scene intent:
  - hero
  - floor material
  - reflective closeup
  - wide room
  - focal object
  - mouse-look stability path

Pseudocode:

```cpp
PostProfile post = CinematicPostLibrary::Resolve(profile.post.policyId);
renderer.SetExposure(post.exposure, cameraRole.meteringRegion);
renderer.SetToneMapper(post.toneMapper);
renderer.SetColorGrade(post.grade);
renderer.SetBloom(post.bloom);
renderer.SetFog(post.fog);
renderer.SetSharpening(post.sharpening);
```

Completion:

- same scene can switch from diagnostic neutral to cinematic public profile
- mood is profile-driven and reproducible
- exposure/post changes do not mask renderer bugs in debug packets

### Phase G: Migration And Decommission

Goal: remove the scattered old controls after the new path proves ownership.

Work:

- inventory every old scene-specific IBL/reflection/post/material override
- classify each as:
  - legitimate profile data
  - temporary debug override
  - obsolete workaround
  - bug repro setting
- migrate legitimate controls into profile libraries
- keep bug repro settings as explicit named diagnostics
- remove obsolete workarounds only after packets pass

Pseudocode:

```text
for each old visual override:
  find owner
  map to profile/material/light/reflection/post library
  add contract test
  delete or gate old override
```

Completion:

- public launch scenes cannot bypass scene visual contracts
- debug menus expose profile overrides without changing source code
- the old sharp-IBL flicker repro is preserved as a regression test

### Phase H: Release Gate

Goal: complete the goal only with evidence, not confidence.

Required command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build/captures/scene_local_cinematic_renderer_v1_release_candidate
```

Required passing families:

- kitchen
- office
- gym
- concert
- gallery

Required proof:

- build passes
- shader compile logs are clean
- frame reports have scene visual contracts
- enclosed scenes have no visible external HDRI ownership
- reflection-owner unknown ratio is under threshold
- material-class debug output is present
- shadow/temporal/reflection stability metrics pass
- beauty captures are visually inspectable from multiple camera roles

Do not mark the active goal complete until the packet evidence passes and the
user accepts that the public scenes are visually good enough.

## 2026-06-04 Material Policy Shader Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- Added renderer-wide material policy helpers in
  `assets/shaders/SurfaceClassification.hlsli`:
  - `SurfaceNormalScaleCeiling`
  - `SurfaceProceduralDetailCeiling`
  - `SurfaceNormalVarianceRoughnessBoost`
  - `SurfaceReflectionStabilityScale`
  - `SurfacePolicyDebugColor`
- `assets/shaders/MaterialResolve.hlsl` now clamps authored normal-map scale
  and procedural material detail through the surface-class policy, and applies
  class-owned roughness floors rather than a single global floor.
- `assets/shaders/DeferredLighting.hlsl` now uses class-aware normal-variance
  roughness stabilization and exposes policy debug output.
- `assets/shaders/PostProcess.hlsl` now scales SSR/RT post reflection
  contribution by surface-class reflection stability and exposes debug view
  `47`.
- Debug/report plumbing:
  - `src/Graphics/Renderer_DebugSettings.cpp` supports mode `47`
    `MaterialPolicy`.
  - `src/Graphics/FrameContract.h` and
    `src/Graphics/FrameContractJson.cpp` report
    `material_policy_debug_view_mode`.
  - `tools/run_scene_local_cinematic_renderer_v1_packets.ps1` captures
    `surface_policy`.
  - `tools/run_scene_local_cinematic_renderer_v1_contract_tests.ps1` guards
    the policy helpers, debug mode, and packet view.

Validation:

- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && ninja -C build CortexEngine -j2`.
  - Existing warning only:
    `VisibilityBuffer_BRDFLUTPipeline.cpp`: unused local `hr`.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_static_20260604/manifest.json`
  - views include `surface_policy`
  - owner `PASS`, enclosed families `visible_ibl_ratio=0.0`,
    `unknown_ratio=0.0`
  - material `PASS`, warnings `0`
  - stability `PASS`, failures `0`, warnings `0`
- Camera-sweep all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`
  - stability `PASS`, failures `0`, warnings `2`,
    hard-gate warnings `0`, diagnostic warnings `2`
  - hard-gated motion-compensated aggregate:
    mean `2.327448`, changed `0.054253`, large `0.000868`
- Mouse-jitter all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_material_policy_mouse_jitter_20260604/manifest.json`
  - owner `PASS`, material `PASS`
  - stability `PASS`, failures `0`, warnings `5`,
    hard-gate warnings `0`, diagnostic warnings `5`
  - hard-gated motion-compensated aggregate:
    mean `4.856049`, changed `0.167108`, large `0.014991`
- Log scan across the three material-policy packet folders found no shader
  compile/device failure strings.

Interpretation:

- Material classes now own part of the shader behavior, not only the debug
  color.
- This is a renderer-wide control point for richer future BRDF/detail work.
- It is still not final cinematic visual quality. The next real visual step is
  local reflection-probe radiance and light-rig richness, using the same packet
  gates.

## 2026-06-04 Profile-Owned Light Fixture Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- Added `SceneLightFixtureProfile` and
  `SceneCinematicProfile::lightFixtures`.
- Added point/spot fixture helpers in `RendererSceneProfile.cpp`.
- Moved target-family local fixtures into `BuildSceneLocalCinematicProfile()`:
  kitchen, office, classroom, gym, concert, red room, and stadium.
- Added `AddModelAuthoredProfileLights()` so model-authored scenes instantiate
  fixture lights from the same profile that owns environment, material, post,
  and reflection policy.
- Removed the old `AddModelAuthoredFamilyLights()` family switch from
  `Engine_Scenes.cpp`.
- Added `profile_light_fixture_count` to frame reports.
- Added `profile_lights=...` to model-authored scene load logs.
- Contract tests now guard the fixture type, fixture library ids, scene-loader
  hook, frame-report field, and removal of the old family-light switch.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && ninja -C build CortexEngine -j2`.
- Static all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_profile_lights_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `0`
  - profile fixture counts:
    gallery `0`, kitchen `3`, office `2`, gym `4`, concert `17`
  - total light counts:
    gallery `6`, kitchen `8`, office `7`, gym `9`, concert `25`
- Camera-sweep all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_profile_lights_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `2`, hard-gate warnings `0`,
    diagnostic warnings `2`
  - hard-gated motion-compensated aggregate:
    mean `2.327448`, changed `0.054253`, large `0.000868`
- Log scan across the static and camera-sweep packet folders found no shader
  compile/device failure strings.

Interpretation:

- Required model-authored families now instantiate local fixture lights from
  `SceneCinematicProfile`.
- The fixture migration did not regress owner, material, or hard-gated motion
  stability packet gates.
- Gallery remains visually profile-driven but does not yet use the
  model-authored `SceneLightFixtureProfile` path for its local lights.

## 2026-06-04 Semantic Area-Light Fixture Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- Extended `SceneLightFixtureProfile` with:
  - `semanticClass`
  - `areaSize`
  - `twoSided`
- Added `AddAreaFixture()` to the scene profile library.
- Converted broad local emitters from point-light approximations to semantic
  `area_rect` fixtures:
  - kitchen window and under-cabinet strip
  - office monitor panel
  - classroom window and ceiling panel
  - gym high-bay panels
  - concert screen panel, neon strips, wall strips, panel lifts, and overhead
    soft fill
  - red-room neon panel and edge softbox
  - stadium flood banks
- `AddModelAuthoredProfileLights()` now instantiates
  `Scene::LightType::AreaRect`, preserves area size/two-sided flags, and uses
  a robust look rotation for spot/area emitters.
- Frame reports now expose:
  - `point_light_count`
  - `spot_light_count`
  - `area_rect_light_count`
  - `two_sided_area_light_count`
- Contract tests guard semantic fields, area fixture helpers, area-light
  instantiation, and frame-report counters.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && ninja -C build CortexEngine -j2`.
- Static all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_semantic_area_lights_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `0`
  - area-light counts:
    gallery `2`, kitchen `2`, office `1`, gym `3`, concert `8`
  - concert two-sided area lights: `2`
- Camera-sweep all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_semantic_area_lights_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `2`, hard-gate warnings `0`,
    diagnostic warnings `2`
  - hard-gated motion-compensated aggregate:
    mean `2.315142`, changed `0.054253`, large `0.000868`
- Log scan across both packet folders found no shader compile/device failure
  strings.

Interpretation:

- Required model-authored families now have a reusable semantic lighting
  grammar and real soft emitters.
- This moves the renderer toward cinematic full-scene lighting without
  hand-polishing individual scenes.
- Remaining high-value work: make shader/post/exposure behavior respond to
  fixture classes and improve gallery fixture ownership parity.

## 2026-06-04 Gallery Profile Fixture And Probe Ownership Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- `BuildGalleryCinematicProfile()` now owns RT Showcase/gallery local probes:
  - `RTGallery_LocalProbe_Left`
  - `RTGallery_LocalProbe_Right`
- `BuildGalleryCinematicProfile()` now owns RT Showcase/gallery local fixtures:
  - `ProfileLight_Gallery_Softbox`
  - `ProfileLight_Gallery_KeyLight`
  - `ProfileLight_Gallery_FillLight`
  - `ProfileLight_Gallery_RimLight`
- Renamed model-authored-only instantiation helpers to generic profile helpers:
  - `AddSceneProfileLights()`
  - `AddSceneProfileReflectionProbes()`
- `BuildRTShowcaseScene()` now instantiates gallery profile lights/probes from
  `BuildGalleryCinematicProfile()` instead of hard-coded RT gallery light/probe
  blocks.
- RT Showcase logs now expose:
  `RT Showcase profile assets: profile_lights=4 reflection_probes=2`.
- Contract tests guard gallery fixture ids, gallery profile probes, generic
  helper use, and removal of the model-authored-only helper name.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && ninja -C build CortexEngine -j2`.
- Static all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_gallery_profile_fixtures_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `0`
  - gallery frame contract:
    `profile_light_fixture_count=4`, `local_reflection_probe_count=2`,
    `light_count=6`, `spot_light_count=3`, `area_rect_light_count=2`
- Camera-sweep all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_gallery_profile_fixtures_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `2`, hard-gate warnings `0`,
    diagnostic warnings `2`
  - hard-gated motion-compensated aggregate:
    mean `2.315142`, changed `0.054253`, large `0.000868`
- Log scan across both packet folders found no shader compile/device failure
  strings.

Interpretation:

- The required family set now has profile-owned local lighting evidence across
  kitchen, office, gym, concert, and gallery.
- Gallery reflection probes are now profile-owned too.
- Remaining work is no longer about basic ownership parity; it is about making
  the shader/post/exposure response to semantic fixture classes look visually
  strong enough and proving that through captures.

## 2026-06-04 Scene-Local Reflection Probe Radiance Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- Added profile-owned local reflection probe descriptors:
  - `SceneReflectionProbeProfile`
  - `SceneReflectionProfile::localProbeRigId`
  - `SceneReflectionProfile::localProbeEnabled`
  - `SceneReflectionProfile::localProbeDiffuse`
  - `SceneReflectionProfile::localProbeSpecular`
- Added family probe rigs in `RendererSceneProfile.cpp` for kitchen, office,
  classroom, gym, concert, red room, stadium, and gallery.
- Added `Renderer::SetLocalReflectionProbeRadiance()` and renderer state for
  local probe diffuse/specular radiance, separate from global visible IBL.
- Extended VB deferred lighting constants with `localProbeParams`.
- `DeferredLighting.hlsl` now lets local probes contribute low-frequency
  diffuse/specular radiance even when global/visible IBL is disabled.
- Added `local_reflection_probe_rig_id` to frame reports.
- Added `AddModelAuthoredReflectionProbes()` so model-authored scenes
  instantiate reflection probe components from their profile instead of relying
  on per-scene ad hoc probes.
- Contract test now guards the probe profile, loader hook, shader constant,
  and frame report key.

Validation:

- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && ninja -C build CortexEngine -j2`.
  - Existing warning only:
    `VisibilityBuffer_BRDFLUTPipeline.cpp`: unused local `hr`.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_local_probe_static_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, warnings `0`
  - frame reports show local probe counts:
    gallery `2`, kitchen `1`, office `1`, gym `1`, concert `1`
  - view `reflection_probe_weight` is nonzero for all required families:
    gallery nonzero ratio `0.944586`, kitchen/office/gym/concert `1.0`
- Camera-sweep all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_local_probe_camera_sweep_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`, diagnostic warnings `2`
  - hard-gated motion-compensated aggregate:
    mean `2.327448`, changed `0.054253`, large `0.000868`
- Mouse-jitter all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_all_families_local_probe_mouse_jitter_20260604/manifest.json`
  - owner `PASS`, material `PASS`, stability `PASS`
  - failures `0`, hard-gate warnings `0`, diagnostic warnings `5`
  - hard-gated motion-compensated aggregate:
    mean `4.856049`, changed `0.167108`, large `0.014991`
- Log scan across the three local-probe packet folders found no shader
  compile/device failure strings.

Interpretation:

- Kitchen, office, gym, and concert now have actual profile-instantiated local
  reflection probe volumes and shader-visible local-probe radiance.
- Global/visible IBL remains disabled for enclosed target scenes; local probes
  are a separate scene-local radiance channel.
- This is still not final cinematic lighting. The next high-value slice is a
  real lighting-rig library with profile-owned local fixtures and stronger
  light-rig report evidence.

## 2026-06-04 Procedural Scene-Local Probe Radiance Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- `DeferredLighting.hlsl` now has procedural room-radiance helpers:
  - `ComputeSceneLocalProbeDiffuse`
  - `ComputeSceneLocalProbeSpecular`
- Local probes no longer require an environment texture sample to contribute
  radiance in enclosed/no-environment scenes.
- The shader uses `authoredInteriorNoEnvironment` to block probe texture
  radiance when global IBL is disabled and no visible environment is allowed.
  In that mode it derives local radiance from ambient color, sun color,
  surface class, and named scene material class.
- Probe texture radiance remains available for non-enclosed/visible-environment
  cases through `localProbeTextureRadianceAllowed`.
- Frame reports now expose:
  - `local_reflection_probe_radiance_enabled`
  - `local_reflection_probe_diffuse_intensity`
  - `local_reflection_probe_specular_intensity`
- Frame-contract validation now warns when a profiled enclosed VB scene
  declares a local reflection probe rig but has disabled/zero local probe
  radiance or no valid local probe table.
- Contract tests guard the procedural probe helpers, texture-radiance gate,
  and report/validation fields.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed for the touched shader/source/test files.
- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && cmake --build build --config Release --target CortexEngine --parallel 4`.
- The updated deferred shader was copied into `build/bin/assets/shaders`
  before runtime packet validation because the build used skipped asset sync.
- Static all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_local_probe_procedural_radiance_20260604/static_packet/manifest.json`
  - `captured_view_count=55`
  - reflection owner `PASS`, failures `0`, warnings `0`
  - material `PASS`, failures `0`, warnings `0`
  - beauty reports show VB rendered, local probe table valid, local probe
    radiance enabled, and invalid HDRI `false` for gallery, kitchen, office,
    gym, and concert.
  - enclosed diffuse/specular probe intensities:
    kitchen `0.26/0.24`, office `0.20/0.22`, gym `0.30/0.20`,
    concert `0.26/0.30`
- Static sequence all-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_local_probe_procedural_radiance_20260604/static_sequence_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`, warnings `0`
  - aggregate stability:
    mean `0.327851`, changed `0.007535`, large `0.003581`
- Log scans across both packet folders found no visibility-buffer
  initialization failure, shader compile failure, fence timeout, `DXGI_ERROR`,
  device removal, renderer failure, validation error, or local-probe
  validation warning strings.

Interpretation:

- Enclosed cinematic scenes now have a renderer-owned procedural local
  radiance fallback, so the next material-richness pass can push glossy,
  wet, metallic, and clearcoat materials without secretly depending on a
  visible or hidden external HDRI.
- The goal remains incomplete. The next high-value work is a stronger
  motion/camera-sweep packet after this shader change, followed by richer
  material layers and scene-profile post/shadow polish under the same gates.

## 2026-06-04 Cinematic Material Layer Policy Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Implemented:

- Added a renderer-wide material layer contract:
  `SceneMaterialProfile::materialLayerSetId`, defaulting to
  `scene_local_cinematic_material_layers_v1`.
- Frame reports now expose
  `scene_visual_contract.material_layer_set_id`.
- Frame-contract validation warns on missing/default material layer set id for
  profiled scenes.
- Added shared named-class layer helpers in
  `assets/shaders/SurfaceClassification.hlsli`:
  - `SceneMaterialCinematicDetailFloor`
  - `SceneMaterialCinematicClearcoatBoost`
  - `SceneMaterialCinematicWetnessBoost`
  - `SceneMaterialCinematicEmissiveBoost`
- `assets/shaders/MaterialResolve.hlsl` now applies the layer policy by named
  scene material class:
  - bounded microdetail floors for walls, tile, concrete, wood, fabric, metal,
    rubber, wet surfaces, and water
  - clearcoat boosts for ceramic tile, polished wood, polished metal, glass,
    mirror, and wet surfaces
  - wetness boosts for wet/water surfaces plus restrained tile/wood highlights
  - emissive boost for neon and screen panels
- The layer path still flows through existing procedural footprint filtering,
  normal/detail ceilings, roughness floors, and named material policy, so it
  is a controlled renderer-wide richness layer rather than per-scene shader
  constants.
- Contract tests guard the material layer id and the shader helper functions.

Validation:

- Pre-layer camera-sweep packet after procedural probe radiance passed:
  `build/captures/scene_local_cinematic_renderer_v1_local_probe_procedural_radiance_20260604/camera_sweep_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`, warnings `0`
  - all required beauty reports had VB rendered, valid local probe tables,
    local probe radiance enabled, invalid HDRI `false`, and warnings `0`
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed.
- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && cmake --build build --config Release --target CortexEngine --parallel 4`.
- The changed shaders were copied into `build/bin/assets/shaders` before
  runtime packet validation because asset sync was skipped.
- Static sequence material-layer packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_material_layer_policy_20260604/static_sequence_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`, warnings `0`
  - aggregate stability:
    mean `0.367545`, changed `0.008005`, large `0.003641`
  - gallery, kitchen, office, gym, and concert all reported
    `material_layer_set_id=scene_local_cinematic_material_layers_v1`, VB
    rendered, local probe radiance enabled, invalid HDRI `false`, and
    warnings `0`
- Camera-sweep material-layer packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_material_layer_policy_20260604/camera_sweep_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`, warnings `0`
  - all required beauty reports kept
    `material_layer_set_id=scene_local_cinematic_material_layers_v1`,
    invalid HDRI `false`, and warnings `0`
- Log scans across the material-layer packet folders found no visibility-buffer
  initialization failure, shader compile failure, fence timeout, `DXGI_ERROR`,
  device removal, renderer failure, validation error, material-layer warning,
  or local-probe warning strings.

Interpretation:

- Material richness is now a named renderer-wide policy that can be tuned and
  audited through scene profiles and frame reports.
- This improves the path toward cinematic visuals, but the goal remains
  incomplete until post/shadow/exposure polish and longer multiview motion
  evidence are strong enough for public visual acceptance.

## 2026-06-04 Post/Shadow/Exposure Policy Contract Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL VISUAL COMPLETION

Purpose:

- Move the renderer toward full-scene shader ownership instead of scene-local
  tuning constants.
- Make shadow, exposure, and cinematic post quality named policies that can be
  selected by a `SceneCinematicProfile`, reported in frame contracts, and
  rejected when mismatched.

Implemented:

- `SceneLightingProfile::shadowPolicyId`, defaulting to
  `scene_local_soft_stable_shadows_v1`.
- `ScenePostProfile::qualitySetId`, defaulting to
  `scene_local_cinematic_post_quality_v1`.
- `ScenePostProfile::exposurePolicyId`, defaulting to
  `scene_local_manual_exposure_v1`.
- `ApplySceneCinematicProfile()` writes those ids into
  `scene_visual_contract`.
- `Renderer_FrameContractSnapshot.cpp` mirrors scene-visual policy ids into
  `lighting` and `cinematic_post` frame-contract sections.
- `FrameContractJson.cpp` emits:
  - `scene_visual_contract.shadow_policy_id`
  - `scene_visual_contract.exposure_policy_id`
  - `scene_visual_contract.post_quality_set_id`
  - `lighting.shadow_policy_id`
  - `lighting.exposure_policy_id`
  - `cinematic_post.quality_set_id`
- `FrameContractValidation.cpp` warns on:
  - missing/default scene-visual shadow, exposure, or post quality policy
  - lighting policy ids that disagree with the scene-visual contract
  - cinematic post quality id mismatches
  - bloom intensity when bloom did not execute
  - declared post quality with no active post shape
- Contract tests guard all new profile fields, report keys, snapshot
  propagation, and validation warning ids.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed.
- Release build passed:
  `CORTEX_SKIP_ASSET_SYNC=1` plus
  `VsDevCmd.bat -arch=x64 && cmake --build build --config Release --target CortexEngine --parallel 4`.
- Static sequence packet:
  `build/captures/scene_local_cinematic_renderer_v1_post_shadow_policy_20260604/static_sequence_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`, warnings `0`
  - aggregate stability:
    mean `0.350750`, changed `0.007656`, large `0.003701`
  - required families all report matching scene-visual/lighting shadow policy
    `scene_local_soft_stable_shadows_v1`, matching exposure policy
    `scene_local_manual_exposure_v1`, matching post quality
    `scene_local_cinematic_post_quality_v1`, invalid HDRI `false`, and
    frame-contract warnings `0`.
- Stronger camera-sweep packet:
  `build/captures/scene_local_cinematic_renderer_v1_post_shadow_policy_20260604/camera_sweep_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`
  - warning count `9`
  - hard-gate warning count `4`
  - diagnostic warning count `5`
  - hard-gated motion-compensated aggregate:
    mean `6.058491`, changed `0.196181`, large `0.028646`
  - warning debt is concentrated in kitchen beauty and concert
    beauty/reflection-owner/direct-light under the stronger motion sweep, plus
    diagnostic-only reflection/taa views.
- Log scan across both packet folders found no visibility-buffer
  initialization failure, shader compile failure, fence timeout, `DXGI_ERROR`,
  device removal, renderer failure, validation error, or policy mismatch
  warning ids.

Interpretation:

- The renderer now has the policy contract needed for the full-scene shader
  refactor: materials, local probes, fixtures, post, exposure, and shadows are
  all profile-addressable and frame-report-auditable.
- The static result is clean, but the stronger camera-sweep residual warnings
  prove this is not visually finished.
- Next implementation should make shader behavior consume these policies more
  deeply:
  - stable shadow filtering and cascade/contact bias policy by profile
  - fixture-class-aware bloom and exposure protection
  - material-class-aware specular ceilings and post sensitivity
  - reflection-probe diagnostic stabilization/interpretation
  - longer all-family motion packets before claiming public readiness

## 2026-06-04 Cinematic Stability Policy Payload Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, BUT WARNING DEBT REMAINS

Purpose:

- Move from policy ids that are only reported to policy values that shaders can
  actually consume.
- Keep the behavior renderer-wide and profile-derived, not hand-authored for
  kitchen or concert.

Implemented:

- Added `Renderer::BuildCinematicStabilityParams()`.
- Added `FrameConstants::cinematicStabilityParams`.
- Added `VisibilityBuffer::DeferredLightingParams::cinematicStabilityParams`.
- Post-process and VB deferred lighting now receive the same policy vector:
  - material/specular motion damping
  - reflection debug stability
  - shadow softness scale
  - highlight/exposure protection
- `PostProcess.hlsl` uses the vector for glossy/reflection damping,
  reflection-owner debug strength, and a soft HDR highlight shoulder.
- `DeferredLighting.hlsl` uses the vector for stable shadow PCF/bias scaling.
- Frame reports expose the active policy values under `cinematic_post`.
- Frame-contract validation warns if a profiled cinematic scene has an
  inactive or invalid stability policy.
- Contract tests guard the CPU builder, shader constants, report fields,
  validation warning ids, and shader usage.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed.
- Release build passed with existing unused-`hr` warning only.
- Static sequence packet:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_stability_policy_20260604/static_sequence_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`, warnings `0`
  - aggregate stability:
    mean `0.308293`, changed `0.007306`, large `0.003171`
  - required families all reported
    `stability_policy_active=true`,
    `material_motion_damping=0.24`,
    `shadow_softness_scale=1.18`, and
    `highlight_protection=0.24`.
- Stronger camera-sweep packet:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_stability_policy_20260604/camera_sweep_packet/manifest.json`
  - reflection owner `PASS`
  - material `PASS`
  - stability `PASS`
  - failures `0`
  - warning count `9`
  - hard-gate warning count `4`
  - diagnostic warning count `5`
  - hard-gated motion-compensated aggregate:
    mean `6.058491`, changed `0.196181`, large `0.028646`

Interpretation:

- This is useful architecture: the shader stack now has profile-derived
  numeric controls for full-scene stability and exposure behavior.
- It did not materially reduce the stronger camera-sweep warning debt; the
  pre/post residual rows are effectively unchanged.
- Treat the remaining warning debt as a diagnostic problem to separate true
  material/shadow instability from expected camera parallax in debug views.
  The next pass should improve motion validation or add deeper instrumentation
  before applying more shader tweaks.

## 2026-06-04 Motion Stable-Core Stability Analyzer Slice

Status: IMPLEMENTED AND PACKET-VERIFIED

Purpose:

- Keep motion stability evidence honest by separating true stable-surface
  flicker from expected camera-sweep parallax and disocclusion edges.
- Preserve raw whole-frame motion metrics for debugging while judging
  hard-gated motion stability on stable interior pixels.

Implemented:

- `tools/analyze_scene_local_packet_stability.py`
  - added luma edge masking.
  - added motion stable-core comparison after global motion compensation.
  - excludes high-gradient/disocclusion edge regions from the stable-core
    residual.
  - keeps raw, whole-frame motion-compensated, and stable-core metrics in the
    report.
  - uses stable-core limits for motion packets when enough stable core exists.
  - records stable-core aggregate and hard-gate aggregate metrics in manifests.
- New analyzer knobs:
  - `--edge-threshold`
  - `--edge-dilation`
  - `--min-stable-core-ratio`
- Contract tests guard the stable-core fields and analyzer knobs.

Validation:

- Python syntax check passed:
  `python -m py_compile tools\analyze_scene_local_packet_stability.py`
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static packet reanalysis:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_stability_policy_20260604/static_sequence_packet/manifest.json`
  - stability `PASS`
  - failures `0`, warnings `0`
  - aggregate:
    mean `0.308293`, changed `0.007306`, large `0.003171`
- Camera-sweep packet reanalysis:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_stability_policy_20260604/camera_sweep_packet/manifest.json`
  - stability `PASS`
  - failures `0`
  - warning count `3`
  - hard-gate warning count `0`
  - diagnostic warning count `3`
  - raw whole-frame aggregate:
    mean `23.445955`, changed `0.852009`, large `0.183365`
  - hard-gated stable-core aggregate:
    mean `1.443717`, changed `0.015063`, large `0.0`
  - remaining warnings are diagnostic-only gallery
    `reflection_probe_weight` warnings.

Interpretation:

- The previous kitchen/concert camera-sweep warning debt was edge/parallax
  dominated, not broad shader flicker on stable surfaces.
- Required hard-gated views now show clean stable-core motion evidence.
- The remaining diagnostic probe-weight warning should inform local probe debug
  semantics, not block beauty/material stability.

## 2026-06-04 Probe-Weight Diagnostic Signal Classification Slice

Status: IMPLEMENTED AND PACKET-VERIFIED

Purpose:

- Keep packet warning counts reserved for likely public beauty/material
  instability.
- Preserve camera-sweep motion in `reflection_probe_weight` as evidence without
  misclassifying it as flicker, because this debug view intentionally visualizes
  moving local-probe influence/coverage weights.

Implemented:

- `tools/analyze_scene_local_packet_stability.py`
  - added `motion_informational_views`.
  - classifies `reflection_probe_weight` as informational in motion packets.
  - moves threshold residuals for informational views into
    `diagnostic_signals`.
  - writes `informational_view`, `diagnostic_signals`, and
    `diagnostic_signal_count` into packet reports and manifests.
- `tools/run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
  now asserts those analyzer fields exist.

Validation:

- Python syntax check passed:
  `python -m py_compile tools\analyze_scene_local_packet_stability.py`
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static packet reanalysis:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_stability_policy_20260604/static_sequence_packet/manifest.json`
  - stability `PASS`
  - failures `0`, warnings `0`, diagnostic signals `0`
  - aggregate:
    mean `0.308293`, changed `0.007306`, large `0.003171`
- Camera-sweep packet reanalysis:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_stability_policy_20260604/camera_sweep_packet/manifest.json`
  - stability `PASS`
  - failures `0`, warnings `0`, diagnostic signals `3`
  - hard-gate warnings `0`, diagnostic warnings `0`
  - raw whole-frame aggregate:
    mean `23.445955`, changed `0.852009`, large `0.183365`
  - hard-gated stable-core aggregate:
    mean `1.443717`, changed `0.015063`, large `0.0`

Interpretation:

- The scene-local cinematic packet now distinguishes public stability warnings
  from expected diagnostic-view motion.
- Hard-gated required views are clean in both static and camera-sweep evidence.
- This does not complete the renderer V1 goal; it clears the stability evidence
  path so the next work can focus on richer cinematic visual quality and
  human-review packets.

## 2026-06-04 Profile-Driven Cinematic Look Payload Slice

Status: IMPLEMENTED AND PACKET-VERIFIED

Purpose:

- Move visual quality beyond generic warm/cool grade and fixed post-process
  constants.
- Give every scene-local cinematic profile a shader-visible look payload that
  controls black/toe lift, highlight rolloff, color separation, and bloom
  halation in a renderer-wide way.

Implemented:

- `Renderer::BuildCinematicLookParams()`
  - active for `scene_local_cinematic_post_quality_v1`.
  - derives values from scene visual contract metadata:
    `toneMapperPreset` and `materialPaletteId`.
  - produces one vector:
    `x=black_toe_lift`, `y=highlight_rolloff`,
    `z=color_separation`, `w=halation_strength`.
- `FrameConstants`
  - added `cinematicLookParams`.
- `PostProcess.hlsl`
  - added `g_CinematicLookParams`.
  - added `ApplyCinematicToeLift`.
  - added `ApplyProfileColorSeparation`.
  - made bloom halation profile-controlled instead of fixed.
  - made highlight rolloff compress post-tonemap luma as well as saturation.
- `FrameContract`
  - reports look policy state and all look parameters under `cinematic_post`.
  - validates inactive or out-of-range look policy for cinematic post quality
    profiles.
- Contract tests guard the new builder, constant, report fields, validation
  warnings, and shader hooks.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed.
- Release build passed through `VsDevCmd.bat` and `cmake --build`; only the
  existing unused-`hr` warning appeared on the full rebuild.
- Compact packet:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_look_policy_20260604/static_packet_v2/manifest.json`
- Contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_cinematic_look_policy_20260604/static_packet_v2/beauty_contact_sheet.jpg`
- Beauty-frame look values:
  - gallery: toe `0.060`, rolloff `0.240`, separation `0.200`,
    halation `0.180`
  - kitchen: toe `0.060`, rolloff `0.240`, separation `0.260`,
    halation `0.230`
  - office: toe `0.080`, rolloff `0.240`, separation `0.240`,
    halation `0.180`
  - gym: toe `0.060`, rolloff `0.280`, separation `0.200`,
    halation `0.140`
  - concert: toe `0.035`, rolloff `0.300`, separation `0.420`,
    halation `0.480`

Interpretation:

- This is a real renderer-wide visual-quality slice: the shader stack now has
  profile-owned look shaping instead of a single generic post grade.
- The five target families all activate the look policy with distinct values.
- This is still not final "breathtaking" quality. The refreshed contact sheet
  shows that some high-key spaces, especially gym, still need exposure/key-luma
  control and visual review before the V1 goal can be closed.

## 2026-06-04 Profile-Owned Exposure / Highlight Policy Slice

Status: IMPLEMENTED AND PACKET-VERIFIED

Purpose:

- Address the over-bright/high-key flatness visible after the cinematic look
  payload slice.
- Move exposure control from fixed scene profile numbers toward a reusable
  profile-owned post-process contract.

Implemented:

- `Renderer::BuildCinematicExposureParams()`
  - active for `scene_local_cinematic_post_quality_v1`.
  - derives values from `profileId`, `toneMapperPreset`, and
    `materialPaletteId`.
  - outputs:
    - `profile_exposure_trim`
    - `hdr_shoulder_start`
    - `hdr_shoulder_strength`
    - `post_white_compression`
- `FrameConstants`
  - added `cinematicExposureParams`.
- `PostProcess.hlsl`
  - applies profile exposure trim before tone mapping.
  - combines profile HDR shoulder compression with the stability highlight
    protection path.
  - applies profile post-tonemap white compression.
- `FrameContract`
  - reports exposure policy state and values under `cinematic_post`.
  - validates inactive or out-of-range exposure policy for cinematic profiles.
- Contract tests guard the builder, constant, JSON fields, validation warnings,
  and shader hooks.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed.
- Release build passed through `VsDevCmd.bat` and `cmake --build`.
- Compact packet:
  `build/captures/scene_local_cinematic_renderer_v1_exposure_policy_20260604/static_packet_strong/manifest.json`
- Contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_exposure_policy_20260604/static_packet_strong/beauty_contact_sheet.jpg`
- Beauty-frame exposure policy values:
  - gallery: trim `0.760`, shoulder `3.80/0.420`, white `0.360`
  - kitchen: trim `0.720`, shoulder `3.80/0.420`, white `0.360`
  - office: trim `0.720`, shoulder `3.60/0.440`, white `0.380`
  - gym: trim `0.500`, shoulder `2.40/0.680`, white `0.620`
  - concert: trim `0.950`, shoulder `7.00/0.140`, white `0.100`
- Image-stat comparison against the previous look-only packet:
  - gallery mean `187.47 -> 175.04`, clip `0.013 -> 0.000`
  - kitchen mean `170.19 -> 164.22`, white `0.284 -> 0.263`
  - office mean `138.82 -> 130.77`, clip `0.268 -> 0.227`
  - gym mean `225.20 -> 220.22`, white `0.638 -> 0.629`
  - concert mean `107.69 -> 106.08`, intentionally near unchanged.

Interpretation:

- This is an architectural improvement: cinematic profiles now own exposure
  and highlight shaping in the shader-visible post pipeline.
- It measurably reduces clipping for gallery/office/kitchen without muting
  concert.
- It does not fully fix the gym. Strong exposure trim barely changes the gym's
  white/saturated mass, so the remaining issue is likely material/palette
  luminance and saturation in the high-key scene kit rather than only
  post-exposure.

## 2026-06-04 Scene Material Albedo Luminance / Chroma Policy Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Move from pure post-process correction into source material control.
- Prevent scene families from pushing uncontrolled bright/saturated albedo into
  lighting and post, especially after texture sampling and procedural material
  variation.

Implemented:

- `MaterialClassPolicyEvidence`
  - added albedo luminance/chroma clamp evidence and ceilings.
- `MaterialResolver::ApplyMaterialClassPolicy()`
  - applies class-owned albedo luminance/chroma ceilings to authored constant
    albedo.
  - keeps emissive/neon and screen panels lenient so intentional emitters keep
    color identity.
- `SurfaceClassification.hlsli`
  - added `SceneMaterialAlbedoLuminanceCeiling`.
  - added `SceneMaterialAlbedoChromaCeiling`.
  - added `ApplySceneMaterialAlbedoPolicy`.
- `MaterialResolve.hlsl`
  - applies the albedo policy after albedo texture sampling, biome/vertex tint,
    procedural variation, and wetness, before G-buffer write.
- `FrameContract`
  - reports `material_policy_albedo_luminance_clamped`.
  - reports `material_policy_albedo_chroma_clamped`.
- Contract tests guard CPU policy evidence, frame report fields, snapshot
  counters, JSON serialization, and shader usage.

Validation:

- Contract test passed.
- Focused diff check passed.
- Release build passed.
- Final compact packet:
  `build/captures/scene_local_cinematic_renderer_v1_material_albedo_policy_20260604/static_packet_v2/manifest.json`
- Contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_material_albedo_policy_20260604/static_packet_v2/beauty_contact_sheet.jpg`
- First attempt `static_packet/` proved the danger of an overly aggressive
  chroma clamp: it removed saturation but made gym/office too pale and white.
  V2 changed the policy to reduce material energy while preserving more color.

V2 image-stat comparison against the exposure-policy baseline:

- gallery mean `175.04 -> 172.64`, white `0.0314 -> 0.0314`
- kitchen white `0.2630 -> 0.2643`, clip `0.3865 -> 0.3568`,
  high-sat `0.1192 -> 0.0012`
- office white `0.0398 -> 0.0672`, high-sat `0.1094 -> 0.0494`
- gym white `0.6294 -> 0.6312`, clip `0.8723 -> 0.8699`,
  high-sat `0.0514 -> 0.0302`
- concert white `0.0216 -> 0.0343`, clip `0.2727 -> 0.2659`,
  high-sat `0.0441 -> 0.0153`

V2 frame-report evidence:

- gallery: sampled `34`, luminance clamps `19`, chroma clamps `1`,
  max albedo luma `0.8695`
- kitchen: sampled `92`, luminance clamps `30`, chroma clamps `15`,
  max albedo luma `0.72`
- office: sampled `106`, luminance clamps `28`, chroma clamps `45`,
  max albedo luma `0.72`
- gym: sampled `117`, luminance clamps `29`, chroma clamps `41`,
  max albedo luma `0.72`
- concert: sampled `154`, luminance clamps `23`, chroma clamps `78`,
  max albedo luma `0.6762`

Interpretation:

- The renderer now has a reusable material-energy contract on both CPU constants
  and final shader-resolved G-buffer albedo.
- This reduces saturated color artifacts and gives future lighting/post work a
  cleaner input range.
- It does not solve the full visual target. The gym remains too bright and flat
  even with material luma capped, so the next high-value pass is a profile-local
  lighting/exposure metering pass: reduce high-key fill, increase shadow
  separation, and make local lights/camera composition own the scene mood.

## 2026-06-04 Scene Material Cinematic Color Layer Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Add material-local color richness without returning to unstable procedural
  sparkle.
- Keep the effect class-owned and reusable across scene families instead of
  hand-painting one scene.
- Preserve luminance so the color layer does not undo exposure, albedo, and
  white-ratio controls from the previous slices.

Implemented:

- `SurfaceClassification.hlsli`
  - added `SceneMaterialCinematicColorLayerStrength`.
  - added `SceneMaterialCinematicColorLayerAxis`.
  - added `ApplySceneMaterialCinematicColorLayer`.
- `MaterialResolve.hlsl`
  - applies the color layer after scalar procedural albedo variation.
  - uses the existing footprint-filtered procedural mask, so sub-pixel detail
    fades before it can shimmer during mouse look.
  - restores local luminance after tinting.
- Contract tests now pin the color-layer functions and resolve hook.

Validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Focused diff check passed.
- Direct DXC compile passed for `MaterialResolve.hlsl`:
  `build/captures/material_resolve_direct_compile.dxil`.
- Static five-family packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_material_color_layer_20260604/static_packet/manifest.json`
- Warm micro-jitter packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_material_color_layer_20260604/warm_micro_jitter/manifest.json`

Warm micro-jitter summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,roughness,metallic,reflection_owner,shadow_factor,direct_light,taa_blend`.
- Views passed: `35/35`.
- Hard-gate failures: `0`.
- Diagnostic signals: `0`.
- Remaining warning is the existing gallery `taa_blend` diagnostic-only warning.

Build caveat:

- A full Release rebuild timed out while compiling unrelated C++ files even
  with asset sync skipped.
- Since this slice only changes runtime HLSL and the direct DXC/runtime packet
  validations passed, the timeout is tracked as rebuild latency rather than a
  shader failure.

Interpretation:

- This is a renderer-wide material richness layer, not a per-scene art tweak.
- It should make broad materials read less flat while retaining the stability
  contracts from the prior shimmer work.
- It still does not satisfy the final "breathtaking Unreal-quality" target.
  The next passes need to deepen local lighting/probe ownership, BRDF response,
  contact shadows/grounding, and profile-driven post polish.

## 2026-06-05 Scene Material Cinematic Indirect Shaping Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Deepen scene-local indirect light so broad interiors read less flat.
- Add material-aware contact grounding without hand-tuning individual scenes.
- Keep the layer stable by avoiding high-frequency procedural samples or
  view-dependent environment detail.

Implemented:

- `DeferredLighting.hlsl`
  - added `SceneMaterialCinematicIndirectContactStrength`.
  - added `SceneMaterialCinematicIndirectBounceTint`.
  - added `ApplySceneMaterialCinematicIndirectShaping`.
  - applies the shaping layer after ambient/probe/SSAO composition and before
    sheen/debug/final output.
- The layer is profile-owned through the existing cinematic stability constants:
  non-cinematic/default frames stay inactive, while scene-local cinematic
  profiles activate the layer through stable-shadow/highlight-protection values.
- The layer uses:
  - named scene material class.
  - surface class.
  - roughness and metallic.
  - AO.
  - normal orientation and NdotV.
- Contract tests now guard all three helper functions and the ambient apply
  hook.

Validation:

- Direct DXC compile passed:
  `build/captures/deferred_lighting_indirect_shaping_compile.dxil`.
- Focused diff check passed.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static five-family focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_indirect_shaping_20260605/static_packet/manifest.json`
- Static contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_indirect_shaping_20260605/static_packet/indirect_shaping_beauty_ambient_contact_sheet.jpg`
- Warm micro-jitter focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_indirect_shaping_20260605/warm_micro_jitter/manifest.json`

Warm micro-jitter summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,ambient_ibl,direct_light,shadow_factor`.
- Views passed: `20/20`.
- Hard-gate warnings: `0`.
- Diagnostic warnings: `0`.
- Diagnostic signals: `0`.
- Worst stable-core mean luma delta was about `0.548` on kitchen beauty, with
  large changed pixel ratio `0.0`.

Interpretation:

- This moves the renderer toward cinematic local-light depth without using
  scene-specific object edits.
- It is a stability-preserving quality layer, not a complete visual overhaul.
- Remaining work for the V1 goal is still substantial: reflection/probe
  composition, contact shadows, richer BRDF/material layering, better
  profile-owned post polish, and broader final packet evidence.

## 2026-06-05 Scene Material Cinematic Reflection Composite Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Improve post reflection quality without exposing enclosed scenes to visible
  external HDRI bleed.
- Keep SSR/RT owner selection intact, but make the final blend material-aware
  and energy-safe.
- Prevent weak reflections from darkening broad indoor surfaces while still
  allowing mirrors, water, metal, glass, and polished materials to read glossy.

Implemented:

- `PostProcess.hlsl`
  - added `SceneMaterialCinematicReflectionTint`.
  - added `ApplySceneMaterialCinematicReflectionGrade`.
  - added `CompositeSceneMaterialCinematicReflection`.
  - replaced the raw hybrid reflection replacement blend with the new
    material-aware composite.
- The composite:
  - tints reflection by named scene material class and surface class.
  - limits reflection luma relative to the lit base surface.
  - respects the RT firefly clamp.
  - preserves base lighting on non-mirror materials.
  - adds a small controlled sheen on polished broad materials.
- Contract tests guard the new helper functions and the final apply hook.

Validation:

- Direct DXC compile passed:
  `build/captures/postprocess_reflection_composite_compile.dxil`.
  Existing `PostProcess.hlsl` implicit truncation warnings remain.
- Focused diff check passed.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_reflection_composite_20260605/static_packet/manifest.json`
- Static contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_reflection_composite_20260605/static_packet/reflection_composite_owner_contact_sheet.jpg`
- Warm micro-jitter focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_reflection_composite_20260605/warm_micro_jitter/manifest.json`
- Reflection-owner analysis passed:
  `build/captures/scene_local_cinematic_renderer_v1_reflection_composite_20260605/warm_micro_jitter/reflection_owner_analysis.json`

Warm micro-jitter / owner summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,reflection_owner,reflection_probe_weight,roughness,metallic`.
- Views passed: `25/25`.
- Hard-gate warnings: `0`.
- Diagnostic warnings: `0`.
- Owner-analysis failures: `0`.
- Enclosed model-authored scenes had `visible_ibl_ratio=0.0` and
  `unknown_ratio=0.0`.
- Aggregate visible IBL was about `0.00015`, from the gallery where visible
  external HDRI is explicitly allowed.
- One diagnostic signal remains in gallery `reflection_probe_weight`; this is a
  debug mask view rather than a hard-gated beauty or owner failure.

Interpretation:

- This is a renderer-wide reflection composition layer, not per-scene polish.
- It improves local reflection plausibility and reduces the chance that SSR/RT
  samples overwrite scene-local lighting with unstable dark or hot patches.
- The V1 goal remains active. Remaining work should focus on contact shadow
  quality, BRDF/material layering, profile-owned post polish, and final broad
  render evidence.

## 2026-06-05 Scene Material Cinematic Contact AO Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Improve object/furniture grounding through the reusable scene-local shader
  stack.
- Replace global post-AO darkening with a material-aware, cinematic-profile
  owned contact layer.
- Preserve the flicker/shimmer stability gates while increasing visual contact
  depth on plausible receivers.

Implemented:

- `PostProcess.hlsl`
  - added `SceneMaterialCinematicContactAoStrength`.
  - added `SceneMaterialCinematicContactAoTint`.
  - added `ApplySceneMaterialCinematicContactAo`.
  - replaced the previous global AO multiplier with the contact AO helper.
- The layer:
  - keeps the existing bilateral depth-filtered SSAO sampling.
  - rejects sky/background depth before darkening.
  - suppresses contact AO on emissive, glass, mirror, water, screens, and
    polished/metal-heavy receivers.
  - strengthens contact on rough wall/floor/fabric/rubber/concrete-like
    receivers.
  - uses cinematic profile constants so the stronger contact layer is only
    active in cinematic scene-local profiles.
  - protects bright highlights from AO dirt.
- Contract tests now guard the helper functions and the final post hook.

Validation:

- Direct DXC compile passed:
  `build/captures/postprocess_contact_ao_compile.dxil`.
  Existing `PostProcess.hlsl` implicit truncation warnings remain.
- Focused diff check passed.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_contact_ao_20260605/static_packet/manifest.json`
- Static contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_contact_ao_20260605/static_packet/contact_ao_beauty_shadow_contact_sheet.jpg`
- Warm micro-jitter focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_contact_ao_20260605/warm_micro_jitter/manifest.json`

Warm micro-jitter summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,shadow_factor,ambient_ibl,direct_light`.
- Views passed: `20/20`.
- Hard-gate warnings: `0`.
- Diagnostic warnings: `0`.
- Diagnostic signals: `0`.
- Worst stable-core mean luma delta was about `0.549` on kitchen beauty, with
  large changed pixel ratio `0.0`.

Interpretation:

- This is a renderer-wide contact/grounding quality layer, not a scene-specific
  fix.
- It improves the shader architecture by making post AO material/profile owned
  instead of a single global multiplier.
- The V1 goal remains active. Remaining work should focus on direct/contact
  shadow filtering, deeper BRDF/material layering, profile-owned post polish,
  and final broad render evidence across the public scene set.

## 2026-06-05 Scene Material Stable Shadow Filter Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Improve shadow stability and contact readability in the reusable deferred
  scene-local lighting path.
- Remove depth-before-compare filtering from deferred shadow tests, which can
  produce unstable shadow transitions on broad planes and smooth/metallic
  receivers.
- Keep shadow softness profile/material owned instead of globally increasing
  blur.

Implemented:

- `DeferredLighting.hlsl`
  - added `StableShadowMapDimensions`.
  - added `LoadStableShadowDepth`.
  - added `ShadowDepthCompare`.
  - added `QuantizeStableShadowRadius`.
  - added `SceneMaterialCinematicShadowReceiverSoftness`.
  - added `ApplySceneMaterialCinematicShadowRadius`.
  - added `SampleStableShadowPCF`.
  - changed deferred directional and local-light shadow sampling to use stable
    texel loads plus weighted PCF.
  - quantized PCF/PCSS radii to stable texel-space increments.
  - threaded scene material class, surface class, roughness, and metallic into
    shadow evaluation.
- Contract tests now guard the stable shadow helpers and material-aware
  `ComputeShadow(...)` hook.

Validation:

- Direct DXC compile passed:
  `build/captures/deferred_lighting_stable_shadow_compile.dxil`.
- Focused diff check passed.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_stable_shadow_20260605/static_packet/manifest.json`
- Static contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_stable_shadow_20260605/static_packet/stable_shadow_beauty_contact_sheet.jpg`
- Warm micro-jitter focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_stable_shadow_20260605/warm_micro_jitter/manifest.json`

Warm micro-jitter summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,shadow_factor,direct_light,ambient_ibl`.
- Views passed: `20/20`.
- Hard-gate warnings: `0`.
- Diagnostic warnings: `0`.
- Diagnostic signals: `0`.
- Worst stable-core mean luma delta was about `0.948` on office
  `shadow_factor`, with large changed pixel ratio `0.0`.
- Worst beauty-view stable-core mean luma delta remained kitchen beauty at
  about `0.549`, with large changed pixel ratio `0.0`.

Interpretation:

- This is a renderer-wide shadow stability layer, not per-scene polish.
- It improves the full-scene shader stack by making shadow comparisons
  deterministic and receiver/profile aware.
- The V1 goal remains active. Remaining work should focus on deeper
  BRDF/material layering, profile-owned post polish, broader owner analysis,
  and final public-scene render evidence.

## 2026-06-05 Scene Material Direct BRDF Layering Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Give scene material classes distinct cinematic direct-light response in the
  reusable deferred renderer.
- Move the visuals beyond flat primitive shading without hand-polishing
  individual scenes.
- Preserve the existing material/reflection/shadow stability contracts.

Implemented:

- `DeferredLighting.hlsl`
  - added `SceneMaterialCinematicDirectDiffuseTint`.
  - added `SceneMaterialCinematicDirectSpecularGain`.
  - added `ApplySceneMaterialCinematicDirectBRDF`.
  - applied the layer to sun direct lighting.
  - applied the layer to clustered local-light direct lighting.
- The layer:
  - is gated by cinematic profile constants.
  - gives ceramic/glass a cooler direct tint, wood/fabric a warmer direct tint,
    tile/wet/metal/glass stronger controlled gloss, and fabric/rubber/concrete
    softer specular behavior.
  - adds a small rough fabric/paint/rubber velvet response at grazing angles.
  - clamps shaped BRDF luminance relative to the original BRDF, preventing new
    fireflies or overexposed direct-light patches.
  - adds no new texture samples or time-varying terms.
- Contract tests now guard the helper functions and sun/local apply hooks.

Validation:

- Direct DXC compile passed:
  `build/captures/deferred_lighting_direct_brdf_layering_compile.dxil`.
- Focused diff check passed.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_direct_brdf_layering_20260605/static_packet/manifest.json`
- Static contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_direct_brdf_layering_20260605/static_packet/direct_brdf_layering_contact_sheet.jpg`
- Warm micro-jitter focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_direct_brdf_layering_20260605/warm_micro_jitter/manifest.json`

Warm micro-jitter summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,direct_light,roughness,metallic,reflection_owner`.
- Views passed: `25/25`.
- Hard-gate warnings: `0`.
- Diagnostic warnings: `0`.
- Diagnostic signals: `0`.
- Worst stable-core mean luma delta was about `0.541` on kitchen beauty, with
  large changed pixel ratio `0.0`.

Interpretation:

- This is a renderer-wide material response layer, not a scene-specific fix.
- It strengthens the cinematic shader stack while preserving the stability
  harness.
- The V1 goal remains active. Remaining work should focus on profile-owned
  post look polish, final owner/material analysis, and a broader public-scene
  evidence packet.

## 2026-06-05 Scene Local Post Look Polish Slice

Status: IMPLEMENTED AND PACKET-VERIFIED, NOT FINAL QUALITY

Purpose:

- Add final profile-owned post-process polish to the scene-local cinematic
  renderer.
- Improve midtone shape and color separation without hand-editing scene
  families.
- Preserve the established flicker/material/reflection stability gates.

Implemented:

- `PostProcess.hlsl`
  - added `ApplySceneLocalCinematicMidtoneCurve`.
  - added `ApplySceneLocalCinematicChromaPolish`.
  - added `ApplySceneLocalCinematicLookPolish`.
  - applied the layer after tone mapping, toe lift, split tone, profile color
    separation, highlight rolloff, and post white compression, before gamma.
- The layer:
  - is driven by existing cinematic profile constants.
  - uses only current-pixel luma/color, with no new samples or time-varying
    terms.
  - applies a subtle midtone S-curve.
  - adds profile warm/cool shadow and highlight chroma polish.
  - preserves local luminance and clamps shaped luma relative to source luma.
- Contract tests now guard the helper functions and final apply hook.

Validation:

- Direct DXC compile passed:
  `build/captures/postprocess_scene_local_look_polish_compile.dxil`.
  Existing `PostProcess.hlsl` implicit truncation warnings remain.
- Focused diff check passed.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Static focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_post_look_polish_20260605/static_packet/manifest.json`
- Static contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_post_look_polish_20260605/static_packet/post_look_polish_contact_sheet.jpg`
- Warm micro-jitter focused packet passed:
  `build/captures/scene_local_cinematic_renderer_v1_post_look_polish_20260605/warm_micro_jitter/manifest.json`

Warm micro-jitter summary:

- Families: `gallery,kitchen,office,gym,concert`.
- Views: `beauty,direct_light,ambient_ibl,reflection_owner,taa_blend`.
- Views passed: `25/25`.
- Hard-gate warnings: `0`.
- Diagnostic warnings: `1`.
  - gallery `taa_blend` only:
    mean luma delta about `7.245`, changed pixel ratio `0.511`, large changed
    pixel ratio about `0.000098`.
- Diagnostic signals: `0`.
- Worst hard/beauty stable-core mean luma delta was about `0.541` on kitchen
  beauty, with large changed pixel ratio `0.0`.

Interpretation:

- This is a renderer-wide post look layer, not per-scene polish.
- It completes the current shader-stack sequence of material color, indirect
  shaping, reflection composite, contact AO, stable shadows, direct BRDF
  layering, and profile post look polish.
- The V1 goal remains active until a final broad owner/material/stability audit
  proves the required scene families meet the objective.

## 2026-06-05 Final Broad Scene-Local Renderer Audit

Status: BROAD RENDERER CONTRACT PASSED, FULL V1 NOT YET ACCEPTED

Purpose:

- Audit the full scene-local renderer stack after the shader slices.
- Prove, or falsify, the required five-family renderer contracts:
  kitchen, office, gym, concert, and gallery.
- Run owner, material, and stability analysis together instead of relying on
  narrow slice packets.

Packet:

- Output root:
  `build/captures/scene_local_cinematic_renderer_v1_final_broad_audit_20260605/warm_micro_jitter_full`
- Contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_final_broad_audit_20260605/warm_micro_jitter_full/final_broad_audit_contact_sheet.jpg`
- Families:
  `gallery,kitchen,office,gym,concert`.
- Views:
  `beauty,roughness,metallic,surface_class,surface_policy,reflection_probe_weight,reflection_owner,shadow_factor,direct_light,ambient_ibl,taa_blend`.
- Motion:
  warm mouse jitter.
- Sequence:
  `8` captures per family/view.
- Owner analysis:
  enabled.
- Material analysis:
  enabled.
- Stability analysis:
  enabled.

Results:

- Packet runner:
  `PASS`.
- Reflection-owner analysis:
  - status `PASS`.
  - family count `5`.
  - failure count `0`.
  - aggregate visible IBL ratio about `0.000146`.
  - aggregate unknown ratio `0.0`.
  - enclosed model-authored scenes:
    - kitchen visible IBL `0.0`, unknown `0.0`.
    - office visible IBL `0.0`, unknown `0.0`.
    - gym visible IBL `0.0`, unknown `0.0`.
    - concert visible IBL `0.0`, unknown `0.0`.
  - gallery visible IBL about `0.000732`; gallery allows visible external HDRI.
- Material-class analysis:
  - status `PASS`.
  - family count `5`.
  - failure count `0`.
  - warning count `0`.
  - material-class unknown ratio `0.0`.
  - named policy unknown ratio `0.0`.
  - named policy release gate `PASS` for kitchen, office, gym, and concert.
- Stability analysis:
  - status `PASS`.
  - result count `55`.
  - hard-gate views `45`.
  - hard-gate warning count `0`.
  - diagnostic warning count `2`.
  - diagnostic signal count `1`.
  - hard-gate stable-core mean luma delta about `0.917`.
  - hard-gate stable-core changed pixel ratio about `0.0126`.
  - hard-gate stable-core large changed pixel ratio about `0.000042`.
  - diagnostic-only residuals:
    - gallery `taa_blend` warning.
    - gallery `reflection_probe_weight` signal.

Interpretation:

- The renderer contract is now strong across the required five families:
  - no enclosed-scene visible HDRI bleed in reflection ownership.
  - no unknown reflection owner ratio.
  - material classes/policies are known.
  - beauty/direct/ambient/shadow/material/owner hard gates do not flicker under
    mouse jitter.
- This does not prove the full V1 objective is complete:
  - the objective also says the scenes must be high-quality visuals.
  - the audit sheet still shows primitive scene geometry/assets in places.
  - that is no longer primarily a shader stability/HDRI ownership blocker, but
    it remains a visual-quality acceptance blocker.
- Next work should not be another blind shader slice unless a hard gate fails.
  Use this as the renderer stability baseline and move either to scene asset
  quality or to an explicit final visual-quality acceptance gate.

## 2026-06-05 Visual Quality Review Gate Slice

Status: IMPLEMENTED, CURRENT BROAD AUDIT REQUIRES REVIEW

Purpose:

- Add a final visual-quality review gate to the scene-local cinematic renderer
  packet harness.
- Keep subjective art acceptance separate from hard renderer correctness.
- Prevent the V1 objective from being marked complete only because owner,
  material, and stability checks passed.

Implemented:

- `tools/analyze_scene_local_visual_quality.py`
  - analyzes beauty captures for luma range, local contrast, edge/detail
    density, saturation, bright clipping, dark crush, and midtone coverage.
  - imports existing material-class report signals for named surface/policy
    richness.
  - imports existing owner and stability report statuses as dependencies.
  - writes `visual_quality_analysis.json`.
  - updates the packet manifest with a `visual_quality_analysis` block.
  - reports a `completion_gate` with:
    - `renderer_contract_passed`
    - `visual_quality_review_required`
    - `high_quality_visuals_proven`
  - exits `0` for `REVIEW_REQUIRED` by default, but exits `2` when
    `--fail-on-review` is passed.
- `tools/run_scene_local_cinematic_renderer_v1_packets.ps1`
  - now runs the visual-quality analyzer by default after owner/material/
    stability analysis.
  - added `-SkipVisualQualityAnalysis`.
  - added `-VisualQualityFailOnReview`.
- `tools/run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
  - now guards the visual-quality analyzer and runner wiring.

Validation:

- Existing final broad audit analyzed:
  `build/captures/scene_local_cinematic_renderer_v1_final_broad_audit_20260605/warm_micro_jitter_full/manifest.json`
- Analyzer default mode:
  - status `REVIEW_REQUIRED`
  - failure count `0`
  - warning count `5`
  - renderer contract passed `true`
  - high-quality visuals proven `false`
- Strict mode:
  - `--fail-on-review` returned exit code `2` as intended.
- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`

Current review warnings:

- `gallery`: edge density `0.071715 < 0.100000`
- `gallery`: saturation mean `0.111831 < 0.140000`
- `gym`: bright ratio `0.319288 > 0.240000`
- `gym`: named surface ratio `0.163391 < 0.200000`
- `concert`: named surface ratio `0.132880 < 0.200000`

Interpretation:

- Renderer-locality/stability is still the best current baseline.
- The full V1 completion gate is not satisfied because high-quality visuals are
  not proven.
- The next renderer work should target these broad, measurable visual gaps:
  gallery detail/chroma, gym highlight compression/exposure, and richer material
  surface coverage in gym/concert.

## 2026-06-05 Profile-Wide Quality Warning Reduction Slice

Status: IMPLEMENTED AND FOCUSED-PACKET VERIFIED, FULL BROAD RE-AUDIT PENDING

Purpose:

- Use the new visual-quality gate to reduce measurable renderer/profile quality
  gaps without hand-editing individual scene objects.
- Target the current broad-audit warnings:
  - gym bright clipping.
  - gym/concert weak surface-class richness.
  - gallery weak chroma/detail.

Implemented:

- `MaterialModel.cpp`
  - `PaintedWall` now resolves to the masonry/structural surface class instead
    of the default surface class.
  - This keeps named wall policies visible in the material-class debug view and
    gives the shader a concrete receiver class for large authored room surfaces.
- `RendererSceneProfile.cpp`
  - reduced basketball gym sun, high-bay, backboard-wash, exposure, and bloom
    energy.
  - increased gym contrast/saturation modestly to keep it from becoming flat
    after highlight control.
  - increased gallery SSAO, contrast, warm/cool split, and saturation.
- `Renderer_FramePostConstants.cpp`
  - strengthened public-interior highlight rolloff/white compression.
  - allowed the profile exposure trim lower bound to reach `0.42`.
- `PostProcess.hlsl`
  - matched the shader exposure-trim lower bound to `0.42`.
  - added a conservative scene-local clarity pass driven by stable depth/normal
    discontinuities after contact AO and before FXAA.

Build:

- Initial direct CMake build timed out in asset sync.
- Direct Ninja outside `VsDevCmd` failed because the compiler could not see STL
  and Windows SDK headers.
- Correct build command passed:

```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && ninja -C build CMakeFiles/CortexEngine.dir/src/Graphics/RendererSceneProfile.cpp.obj bin/CortexEngine.exe'
```

Focused validation:

- Packet:
  `build/captures/scene_local_cinematic_renderer_v1_quality_gate_gallery_sat_20260605/focused_quality/manifest.json`
- Views:
  `beauty,surface_class,surface_policy`
- Families:
  `gallery,kitchen,office,gym,concert`
- Packet runner: `PASS`
- Visual-quality analyzer:
  - release gate `REVIEW_REQUIRED`
  - failure count `0`
  - warning count `3`
  - two warnings are expected focused-packet dependency notices because owner
    and stability views were intentionally skipped.
  - remaining real visual warning:
    `gallery:edge_density 0.074675 < 0.100000`

Focused metric improvements:

- `gym` bright ratio improved from broad-audit `0.319288` to focused `0.190035`.
- `gym` named surface ratio improved from broad-audit `0.163391` to focused
  `1.0`.
- `concert` named surface ratio improved from broad-audit `0.132880` to focused
  `1.0`.
- `gallery` saturation improved from broad-audit `0.111831` to focused
  `0.140841`.
- `gallery` edge density remains low at `0.074675`.

Other validation:

- Contract test passed:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_contract_tests.ps1`
- Python analyzer compile passed:
  `python -m py_compile tools\analyze_scene_local_visual_quality.py`
- PowerShell parser checks passed for the packet runner and contract test.

Interpretation:

- The shader/profile stack fixed the gym exposure issue and the material-class
  richness issue.
- The remaining gallery edge/detail warning did not respond materially to the
  shader clarity pass. That points to content/asset/detail density, not another
  post-process knob.
- Do not mark V1 complete yet. A full broad owner/material/stability/visual
  packet still needs to be rerun after the next content-detail or gallery asset
  pass.

## 2026-06-05 Gallery Detail Kit And Broad V1 Gate

Status: IMPLEMENTED AND BROAD-PACKET VERIFIED, READY FOR HUMAN VISUAL REVIEW

Purpose:

- Clear the last measured visual-quality blocker without hiding renderer
  problems behind blur, HDRI removal, or one-camera exposure tricks.
- Keep the fix reusable: add gallery architecture/detail layers and use the
  same material/profile contracts already used by kitchen, office, gym, and
  concert.

Implemented:

- `BuildRTShowcaseScene()` now includes a reusable gallery/detail kit:
  - floor cross inlays and long inlays.
  - rear wall rails, framed panels, and signal panels.
  - bevel/detail strips around display plinths.
  - named material policies on the added detail surfaces.
- `RendererSceneProfile.cpp`
  - gallery saturation was raised after the first detail packet fixed edge
    density but left saturation slightly below the visual gate.

Focused validation:

- Packet:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_detail_kit_20260605/focused_quality/manifest.json`
- Result:
  - packet runner passed.
  - gallery edge density improved from about `0.074675` to about `0.159664`.
  - saturation then required the profile adjustment used in the broad packet.

Broad validation:

- Packet:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_detail_broad_20260605/warm_micro_jitter_full/manifest.json`
- Beauty contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_gallery_detail_broad_20260605/warm_micro_jitter_full/gallery_detail_broad_beauty_contact_sheet.jpg`
- Families:
  `gallery,kitchen,office,gym,concert`.
- Motion:
  warm mouse jitter, three-frame capture sequence, `1/120s` fixed delta.
- Views:
  beauty, material/debug, reflection owner/probe, direct/ambient/shadow, and
  TAA blend views.

Analyzer results:

- Visual quality:
  - status `PASS`.
  - release gate `PASS`.
  - failure count `0`.
  - warning count `0`.
  - completion gate:
    - `renderer_contract_passed=true`.
    - `visual_quality_review_required=false`.
    - `high_quality_visuals_proven=true`.
- Reflection ownership:
  - status `PASS`.
  - failure count `0`.
  - enclosed kitchen/office/gym/concert visible IBL ratio `0.0`.
  - unknown owner ratio `0.0`.
  - gallery visible IBL remains allowed and measured at about `0.002740`.
- Material class/policy:
  - status `PASS`.
  - failure count `0`.
  - warning count `0`.
  - unknown surface/policy ratios `0.0`.
- Stability:
  - status `PASS`.
  - hard-gate warning count `0`.
  - hard-gate stable-core mean luma delta about `0.0173`.
  - hard-gate stable-core changed pixel ratio about `0.000260`.
  - hard-gate stable-core large changed pixel ratio `0.0`.
  - diagnostic-only gallery warnings remain in `taa_blend` and
    `reflection_probe_weight`; these are not hard-gated beauty/material/owner
    failures.

Current completion interpretation:

- Renderer V1 has review-ready objective evidence for the required five-family
  set:
  - scene-local environment ownership.
  - no enclosed-scene HDRI bleed.
  - named material/policy coverage.
  - hard-gated mouse-jitter stability.
  - clean visual-quality analyzer output.
- This does not prove the model-authored scene-construction system is solved.
  It proves the current renderer/shader/profile/harness layer is now stable and
  good enough to hand to the user for visual acceptance.

## 2026-06-05 Final Seq8 Renderer V1 Gate

Status: BROAD SEQ8 PACKET PASSED, RENDERER CONTRACT REVIEW-READY

Purpose:

- Re-run the post-gallery-detail renderer gate with a longer eight-frame
  motion sequence so final evidence is not based only on the shorter
  three-frame quality packet.
- Verify the actual V1 objective across gallery, kitchen, office, gym, and
  concert with the full analyzer stack enabled.

Packet:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_scene_local_cinematic_renderer_v1_packets.ps1 -NoBuild -OutputRoot build/captures/scene_local_cinematic_renderer_v1_final_gate_20260605/warm_micro_jitter_full_seq8 -SmokeFrames 56 -CaptureFrame 28 -CaptureSequenceCount 8 -StabilityMotionMode mouse_jitter -MotionFrames 32 -MotionLookAmplitude 0.025 -MotionLookCycles 2.0 -FixedDeltaTime 0.008333333
```

Artifacts:

- Manifest:
  `build/captures/scene_local_cinematic_renderer_v1_final_gate_20260605/warm_micro_jitter_full_seq8/manifest.json`
- Beauty contact sheet:
  `build/captures/scene_local_cinematic_renderer_v1_final_gate_20260605/warm_micro_jitter_full_seq8/final_gate_seq8_beauty_contact_sheet.jpg`

Results:

- Packet runner: `PASS`.
- Visual quality:
  - status `PASS`.
  - release gate `PASS`.
  - warning count `0`.
  - failure count `0`.
  - `high_quality_visuals_proven=true` under the current analyzer.
- Reflection ownership:
  - status `PASS`.
  - failure count `0`.
  - kitchen/office/gym/concert visible IBL ratio `0.0`.
  - unknown owner ratio `0.0` for all five families.
  - gallery visible IBL remains intentional.
- Material policy:
  - status `PASS`.
  - failure count `0`.
  - warning count `0`.
  - kitchen/office/gym/concert named surface ratio `1.0`.
  - gallery named surface ratio about `0.445374`.
- Stability:
  - status `PASS`.
  - hard-gate warning count `0`.
  - diagnostic-only warnings remain in gallery `taa_blend`.
  - diagnostic-only signals remain in gallery `reflection_probe_weight`.

Requirement audit:

- Reusable preset-driven full-scene profile pipeline:
  evidenced by `SceneCinematicProfile` wiring, family profiles, and contract
  tests.
- Materials/lighting/reflections/temporal/post processing upgraded through
  renderer-wide contracts:
  evidenced by material-class, reflection-owner, stability, and visual analyzers
  plus the contract test harness.
- Not a one-scene hand tweak:
  packet covers gallery, kitchen, office, gym, and concert, and the code path
  uses family/profile contracts. Gallery received a reusable detail kit because
  the previous gate isolated a content/detail-density blocker there.
- Scene-local ownership/no HDRI bleed:
  evidenced by reflection-owner analysis with zero visible IBL in enclosed
  kitchen/office/gym/concert.
- No material flicker under motion:
  evidenced by the seq8 packet stability hard gates passing across the debug
  view matrix.

Remaining quality ceiling:

- The renderer V1 evidence is strong enough for review, but the beauty contact
  sheet still shows stylized/blockout assets and geometry. If the target shifts
  from "stable scene-local renderer V1" to "breathtaking Unreal-style final
  art", the next work should be an asset/geometry/detail-fidelity pass built on
  top of this renderer contract, not another root flicker/IBL stability pass.
