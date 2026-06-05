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

Validation:

```powershell
python tools\validate_full_scene_shader_pipeline_v2_plan.py
python -m py_compile tools\validate_full_scene_shader_pipeline_v2_plan.py
```

Current interpretation:

- This shifts the next work from profile/post tweaks into a full renderer
  architecture refactor.
- The plan explicitly forbids hiding problems by disabling IBL, shadows,
  reflections, or temporal history.
- The next implementation slice should add frame-report placeholders for V2
  domains, then upgrade material/asset evidence before changing visual output.
- Renderer V1 remains the baseline. V2 work must preserve the final seq8 packet
  gates or provide stronger replacement evidence.

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
```

## Git Policy

- Commit and push focused work often.
- Do not blanket-stage the dirty worktree.
- Stage only files touched for the current AAA asset-quality slice.
- If GitHub push fails due credentials/network, keep the local commit and
  record the failure here.
