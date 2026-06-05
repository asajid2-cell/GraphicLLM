# AAA Asset Quality Handoff

This is the living handoff for the AAA asset-quality goal.
Read this after compaction before continuing.

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
