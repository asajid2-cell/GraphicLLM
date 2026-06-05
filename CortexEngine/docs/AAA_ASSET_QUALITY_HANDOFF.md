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
