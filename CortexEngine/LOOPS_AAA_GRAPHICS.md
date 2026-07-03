# Loops: AAA Graphics Pass

## Grand Goal Contract

All criteria below must be true for the autonomous portion to be complete:

- Known-bad campsite fixture fails the new graphics gate for missing terrain relief, contact/grounding, material pass, and runtime graphics evidence.
- New campsite render passes both `scene_quality_gate.py` and the new graphics gate.
- At least two novel exterior prompts pass the graphics gate without per-prompt code edits.
- Strengthened graphics gate requires advanced shader material terms, surface/occlusion layering, and runtime shot-camera evidence.
- Generated exterior graphics gate requires texture-backed material fidelity evidence for terrain/ground, rock/cliff, wood, fabric, and hero surfaces, not only colored procedural overlays.
- Regression bundle stays green: C++ Release build, Python compile, v3 campsite/alpine/desert gates, and legacy kitchen smoke.
- `HUMAN-GATE`: user decides whether the new stills are sufficiently AA/AAA.

## Loop Contracts

### Loop 1: Graphics Gate

Invariant: the known-bad flat/blockout campsite render is rejected by a graphics-fidelity verifier.

Entry: known-bad PNG/IR exist under `build/bin/logs`.

Scope:

- in: `tools/scene_graphics_gate.py`, ledger updates.
- out: generator and renderer behavior.

Verifier:

- `python tools\scene_graphics_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\v3_campsite_ridge_test_0_ir.json --png build\bin\logs\v3_campsite_ridge_test_0.png --expect-fail`

Exit: command exits 0 in `--expect-fail` mode and records the expected graphics failure codes.

Escape: if image-only metrics prove too brittle, require IR/runtime evidence and mark image metrics partial.

Status: done

### Loop 2: Terrain And Grounding Runtime Slice

Invariant: generative exteriors render non-flat terrain and explicit contact/shore grounding.

Scope:

- in: `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, possibly `tools/scene_graphics_gate.py` only if re-proving the gate.
- out: unrelated scenes/render passes.

Verifier:

- C++ Release build.
- campsite prompt render.
- `scene_quality_gate.py` green.
- `scene_graphics_gate.py` green with runtime log evidence for heightfield terrain and contact/shore layers.

Status: done

### Loop 3: Material, AO, And Look Slice

Invariant: v3 exterior IR and runtime renderables carry material detail controls and high-quality AO/shadow/SSR settings.

Scope:

- in: `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, `tools/scene_graphics_gate.py`.
- out: broad renderer rewrites.

Verifier:

- graphics gate requires material-pass evidence.
- render log or debug metadata proves SSAO/SSR/shadow controls are enabled for generated exterior.

Status: done

### Loop 4: Novel Prompt Synthesis

Invariant: the graphics pass generalizes beyond the campsite prompt.

Verifier:

- `python tools\scene_gen.py "a stormy alpine lake with a small cabin and blue moonlight" --no-critic --name aaa_graphics_alpine --fast`
- `python tools\scene_gen.py "a sunny desert canyon campsite with red rocks and a turquoise river" --no-critic --name aaa_graphics_desert --fast`
- quality + graphics gates green for both.

Status: done

### Loop 5: World And Shot Fidelity Gate

Invariant: the graphics verifier rejects the current blockout-like desert/canyon baseline even though the older terrain/contact/material checks are green.

Scope:

- in: `tools/scene_graphics_gate.py`, ledgers.
- out: renderer/compiler behavior until the gate is proven red.

Verifier:

- `python tools\scene_graphics_gate.py --prompt "a sunny desert canyon campsite with red rocks and a turquoise river" --ir build\bin\logs\aaa_graphics_desert_0_ir.json --png build\bin\logs\aaa_graphics_desert_gate.png --log build\bin\logs\aaa_graphics_desert_gate.out`

Exit: command exits non-zero with structural failure codes such as `missing_world_depth_geometry`, `desert_canyon_blockout`, `insufficient_material_zone_variation`, or `tree_heavy_desert_staging`.

Escape: if image metrics are too noisy, rely on IR/runtime evidence and keep image metrics as supporting checks only.

Status: done

### Loop 6: Procedural World Geometry And Lighting Slice

Invariant: generated exteriors carry and render first-class world geometry, material-zone, shot-depth, and manipulated-lighting evidence.

Scope:

- in: `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, unrelated renderer architecture.

Verifier:

- Python compile.
- C++ Release build.
- Campsite, alpine, and desert/canyon prompts render and pass quality + strengthened graphics gates.
- Runtime logs include world geometry, canyon wall/foreground occluder, material pass, and lighting pass evidence where required.
- Legacy kitchen smoke remains valid and nonblank.

Exit: all verifier commands green, with artifacts recorded.

Escape: if procedural world geometry destabilizes render captures, reduce the geometry budget first; do not re-enable forced DXR.

Status: done

### Loop 7: Shader Material And Occlusion Layers

Invariant: generated exteriors carry shader-backed material terms and visible/runtime occlusion/surface detail layers, not just object semantics.

Scope:

- in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, broad renderer rewrites.

Verifier:

- Current Loop 6 campsite artifact fails strengthened graphics gate with `missing_advanced_shader_materials` and `missing_occlusion_surface_layers`.
- New campsite, desert, and alpine prompts render and pass quality + strengthened graphics gates.
- Runtime logs include `graphics shader material pass`, `created occlusion layering`, and `created surface detail`.

Status: done

### Loop 8: Adaptive Hero Camera Profiles

Invariant: generated exteriors use prompt-appropriate hero-scale camera profiles so scenes do not read as tiny distant blockouts or cropped walls.

Scope:

- in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`.
- out: prompt semantics, quality gate weakening, unrelated camera systems.

Verifier:

- Current Loop 7 campsite artifact fails strengthened graphics gate with `missing_shot_camera_pass`.
- Campsite/desert prompts pass with the closer midground hero camera.
- Cabin/alpine prompts pass with the balanced cabin hero camera and do not crop the cabin into a wall.

Status: done

### Loop 9: Procedural Asset Fidelity Detail

Invariant: generated exteriors render visible close-range hero construction and richer backdrop silhouettes, not just valid catalog props plus material metadata.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `tools/scene_gen.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, broad renderer architecture, unrelated interior scenes.

Verifier:

- Current Loop 9 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_asset_fidelity_detail`.
- New campsite, desert, and alpine/cabin prompts render and pass quality + strengthened graphics gates.
- Runtime logs include `created hero asset detail` and `created backdrop silhouette detail`.
- Kitchen smoke remains valid and nonblank.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if dense procedural detail destabilizes generated captures, reduce mesh counts and keep the fidelity contract focused on visible hero/backdrop detail; do not re-enable forced DXR.

Status: done

### Loop 10: Atmospheric And Cliff Geometry Realism

Invariant: generated exteriors with storm/moonlight/canyon prompts render authored atmosphere and non-planar cliff detail, not just cool color grading and wall-count metadata.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, unrelated interior scenes.

Verifier:

- Current Loop 13 alpine artifact fails strengthened graphics gate with `missing_atmospheric_time_of_day`.
- Current Loop 13 desert canyon artifact fails strengthened graphics gate with `planar_cliff_geometry`.
- New alpine and desert prompts render and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: atmospheric pass` and `generative_exterior: created cliff erosion detail` where required.
- Kitchen smoke remains valid and nonblank.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if weather overlay geometry hurts semantic/color gates, keep the runtime lighting/fog controls and reduce overlay counts before changing verifier thresholds.

Status: done

### Loop 11: Catalog Cliff Asset Integration

Invariant: canyon generated exteriors use real catalog cliff/rock assets for visible silhouette mass, not only procedural wall planes and overlay lines.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, focused ledgers.
- out: C++ renderer changes, quality gate changes, forced DXR defaults, unrelated prompt classes.

Verifier:

- Current Loop 14 desert canyon artifact fails strengthened graphics gate with `missing_catalog_cliff_assets`.
- New desert canyon prompt renders VALID and passes quality + strengthened graphics gate with at least four `cliff_*` catalog assets in the IR.
- Campsite/alpine regressions remain valid and green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if catalog cliff assets destabilize layout validity, reduce count/footprint and keep procedural cliff detail as fallback.

Status: done

### Loop 12: Surface Material Richness And Ground Integration

Invariant: generated exteriors must show visible material breakup and surface integration: ground stains, rock/lichen or desert stratification decals, wood/fabric detail lines, and vegetation/scrub clusters that reduce the toy-like flat-surface read.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, unrelated interior scenes, external asset downloads.

Verifier:

- Current Loop 17 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_surface_material_richness`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: created material breakup decals` and `generative_exterior: created vegetation surface clusters`.
- Director IR validation and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if surface overlays make scenes noisy or hurt water/color gates, reduce alpha/counts and keep the IR/runtime contract rather than weakening the gate.

Status: done

### Loop 13: Mesh Silhouette Realism

Invariant: generated exteriors must not rely only on flat cliff sheets and boxy hero props; canyon walls need faceted vertical mesh bands/overhangs, and campsite/cabin hero geometry needs bevel/eave/hem silhouette detail visible in the runtime path.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, external asset downloads, unrelated interiors.

Verifier:

- Current Loop 20 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_mesh_silhouette_realism`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: created faceted cliff mesh` for canyon prompts and `generative_exterior: created hero silhouette bevel detail` for campsite/cabin prompts.
- Release build, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if extra mesh bands or hero bevels destabilize captures, reduce counts before changing verifier thresholds.

Status: done

### Loop 14: Naturalistic Ecology Assets

Invariant: generated exteriors must not rely only on low-poly repeated trees, simple grass cards, and primitive debris; the runtime path should stage existing scanned naturalistic grass, fern/bush, branch, stump, trunk, and moss/rock assets as foreground and flank ecology.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, external asset downloads, unrelated interiors.

Verifier:

- Current Loop 21 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_naturalistic_ecology_assets`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: created naturalistic ecology assets` with scanned asset instance counts.
- Release build, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if scanned assets fail to load/upload or make desert prompts read as forest, reduce per-biome counts or switch desert instances to dry branches/stumps/rocks before changing thresholds.

Status: done

### Loop 15: Deep Contact Occlusion

Invariant: generated exteriors must show visible dark contact/receiver shadows under hero props and foreground objects, not only IR/runtime AO claims.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, broad renderer rewrites, unrelated interiors.

Verifier:

- Current Loop 23 campsite/desert artifacts fail strengthened graphics gate with `weak_contact_shadow_image_metric` and/or missing `image_contact_occlusion`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: created image contact occluders`.
- Release build, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if dark contact patches become visible black stains, reduce radius/count/alpha while keeping the dark-contact image threshold.

Status: done

### Loop 16: Water Shore And Soft Occlusion

Invariant: water prompts must show authored shore/water integration and all generated exteriors must show broad terrain-toned soft contact occlusion, without forcing DXR by default.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, unrelated interiors.

Verifier:

- Current Loop 28 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_water_shore_integration_pass` and `missing_soft_occlusion_pass`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: created water shore integration` and `generative_exterior: created soft contact occlusion`.
- Release build, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if soft occlusion reads as black puddles or breaks contact/color/visibility gates, reduce overlay darkness and preserve only small hard contact cores before changing thresholds.

Status: done

### Loop 17: Hero Environment Geometry Fidelity

Invariant: generated exteriors must include a heavier hero/environment geometry construction pass that attacks the current low-poly kit ceiling: richer tent/cabin/camp construction, non-flat mountain/cliff massing, close shoreline/foreground prop geometry, and irregular tree/vegetation silhouette support.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused ledgers.
- out: `tools/scene_quality_gate.py`, forced DXR defaults, broad asset-ingest rewrites, unrelated interiors.

Verifier:

- Current Loop 16 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_hero_environment_geometry`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs include `generative_exterior: created hero environment geometry`.
- Runtime logs include at least one of the prompt-relevant detail tokens: `created high detail camp kit`, `created high detail cabin kit`, `created mountain massing geometry`, or `created irregular tree silhouette geometry`.
- Release build, Python compile, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts and logs recorded.

Escape: if added geometry causes capture timeouts, visual clutter, or object visibility regressions, reduce counts and keep the contract focused on prompt-relevant systems rather than weakening existing gates.

Status: done

### Loop 18: Texture-Backed Material Fidelity

Invariant: generated exteriors bind real local texture/PBR material sources to major visible surfaces instead of relying only on flat colors, line overlays, and procedural metadata.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused asset/material path wiring if needed, ledgers.
- out: weakening `tools/scene_quality_gate.py`, forced DXR defaults, unrelated scene families, external downloads.

Verifier:

- Current Loop 17 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_texture_material_fidelity`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs prove texture-backed material binding for terrain/ground, rock/cliff, wood/fabric/hero pieces, and water/shore-adjacent surfaces where applicable.
- Release build, Python compile, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts/logs recorded, heartbeat retired or rearmed for the next loop, and checkpoint committed.

Escape: if local texture sets cannot be loaded safely in the generated path, fall back to a narrower source-bound material contract that proves existing runtime texture hooks are used for at least terrain/rock/wood; do not weaken color or graphics gates.

Status: done

### Loop 19: Source-Bound Hero Geometry

Invariant: generated exterior hero setpieces use real loaded local source meshes for close prompt anchors and props instead of only primitive/detail overlays.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused local scanned-asset placement, ledgers.
- out: weakening semantic/graphics gates, external downloads, forced DXR defaults, broad asset-registry rewrites.

Verifier:

- Current Loop 18 campsite/desert/alpine artifacts fail strengthened graphics gate with `missing_source_bound_hero_geometry`.
- New campsite/desert/alpine prompts render VALID and pass quality + strengthened graphics gates.
- Runtime logs prove scanned/source hero meshes were loaded and placed for lanterns, utility props, anchor rocks, and hero anchors.
- Release build, Python compile, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts/logs recorded and checkpoint committed.

Escape: if scanned assets crop the water/color ROI or make prompt heroes unreadable, move/scale the source anchors before changing gate thresholds.

Status: done

### Loop 20: Renderer-Level Shadow/Occlusion Budget

Invariant: generated exteriors prove a bounded renderer-level shadow/occlusion budget instead of relying mainly on receiver decals and overlay geometry for contact depth.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, renderer control plumbing and runtime telemetry already present in the generated-exterior path.
- out: weakening image/color gates, forcing DXR/RT by default for generated captures, broad renderer rewrites, external downloads.

Verifier:

- Current Loop 19 campsite/desert/alpine artifacts fail a strengthened graphics gate with a new renderer-shadow/occlusion-budget failure.
- New campsite/desert/alpine prompts render VALID and pass quality + graphics gates.
- Runtime logs prove bounded renderer-level shadow/AO/contact settings reached the engine, plus image contact metrics remain above the existing thresholds.
- Release build, Python compile, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, with artifacts/logs recorded and checkpoint committed.

Escape: if true renderer-level controls cannot be isolated without enabling unsafe DXR defaults, narrow to SSAO/shadow-map/contact telemetry first; do not fake the pass with more decorative overlays.

Status: done

### Loop 21: Composition And Staging Fidelity

Invariant: generated exteriors must stage prompt-critical hero elements as a coherent authored scene rather than a metric-passing scatter of repeated props, disconnected planks/logs, and generic pink camp/cabin kits.

Scope:

- in: `tools/scene_graphics_gate.py`, `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, focused generated-exterior staging/material placement.
- out: weakening semantic/color gates, forced DXR defaults, unrelated interiors, broad asset-ingest rewrites.

Verifier:

- Current Loop 20 campsite/desert/alpine artifacts fail the strengthened graphics gate with a new composition/staging fidelity failure.
- New campsite/desert/alpine prompts render VALID and pass quality + graphics gates.
- Runtime logs prove prompt-aware hero staging, clutter suppression, and palette/material staging reached the generated-exterior runtime path.
- Release build, Python compile, Director IR validation, known-bad oracles, and kitchen smoke remain green.

Exit: all verifier commands green, artifacts/logs recorded, heartbeat rearmed or retired deliberately, and checkpoint committed.

Escape: if image-level clutter metrics are too brittle, keep them as supporting diagnostics and require IR/runtime evidence plus visual artifact inspection; do not hide disconnected staging by weakening color/quality gates.

Status: running

## Progress Log

2026-07-03:

- Created this separate loop ledger to avoid contaminating the Director IR v3 ledger.
- Loop 18 opened after Loop 17 checkpoint and renewed user pushback against early stopping. Heartbeat proof `aaa-loop18-proof` fired by timeout after 1s; self-owned Codex heartbeat `aaa-loop18` armed with resume id from `CODEX_THREAD_ID`. Next action: recon local texture/PBR assets and runtime material APIs, then strengthen the graphics gate so Loop 17 artifacts fail `missing_texture_material_fidelity`.
- Loop 18 red proof:
  - `python tools\scene_graphics_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\aaa_graphics_campsite_loop17b_0_ir.json --png build\bin\logs\aaa_graphics_campsite_loop17b_0.png --log build\bin\logs\aaa_graphics_campsite_loop17b_0.out` exited 1 with `missing_texture_material_fidelity`.
  - `python tools\scene_graphics_gate.py --prompt "a sunny desert canyon campsite with red rocks and a turquoise river" --ir build\bin\logs\aaa_graphics_desert_loop17b_0_ir.json --png build\bin\logs\aaa_graphics_desert_loop17b_0.png --log build\bin\logs\aaa_graphics_desert_loop17b_0.out` exited 1 with `missing_texture_material_fidelity`.
  - `python tools\scene_graphics_gate.py --prompt "a stormy alpine lake with a small cabin and blue moonlight" --ir build\bin\logs\aaa_graphics_alpine_loop17_0_ir.json --png build\bin\logs\aaa_graphics_alpine_loop17_0.png --log build\bin\logs\aaa_graphics_alpine_loop17_0.out` exited 1 with `missing_texture_material_fidelity`.
- Loop 18 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.texture_material_fidelity` counts for terrain/ground, rock/cliff, wood, fabric, hero, and shore surfaces.
  - `tools\scene_graphics_gate.py` now requires the IR contract plus parsed runtime texture-material evidence, and the old graphics oracle with an explicit empty log now fails with `missing_texture_material_fidelity`.
  - `src\Core\Engine_Scenes.cpp` now binds existing local Polyhaven/naturalistic texture sources into generated terrain, shore, rock, wood, fabric, and hero surfaces and logs texture-backed material counts.
  - The visible contact/receiver artifact fix was kept in the runtime path by neutralizing receiver colors toward terrain materials and shrinking hard contact patches instead of weakening graphics or semantic gates.
- Loop 18 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release rebuild exited 0: `[OK] Build complete in 4.4s` (`ninja: no work to do` after prior rebuild).
  - `git diff --check` exited 0.
  - Campsite `aaa_graphics_campsite_loop18d` rendered VALID; quality gate exited 0 (`purple_fraction=0.8836`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_fraction=0.0044`, `dark_contact_area_fraction=0.0298`); Director IR validation exited 0. Runtime texture log: `terrain=3 rock=31 wood=34 fabric=29 hero=79 shore=12 texture_sets=7`.
  - Desert canyon `aaa_graphics_desert_loop18b` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4212`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_area_fraction=0.0128`); Director IR validation exited 0. Runtime texture log: `terrain=3 rock=106 wood=22 fabric=29 hero=81 shore=12 texture_sets=7`.
  - Alpine cabin `aaa_graphics_alpine_loop18b` rendered VALID; quality gate exited 0 (`avg_luma=0.1408`, `cool_fraction=0.889`, `nonblack_fraction=0.9976`); graphics gate exited 0 (`dark_contact_fraction=0.0117`); Director IR validation exited 0. Runtime texture log: `terrain=3 rock=43 wood=81 fabric=0 hero=109 shore=12 texture_sets=6`.
  - Kitchen smoke `regression_kitchen_aaa_loop18b` rendered VALID and quality gate exited 0 (`avg_luma=0.4742`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports forbidden fridge, missing mountain/ridge, focal visibility, and purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with explicit empty log and `--expect-fail` exited 0 and now includes `missing_texture_material_fidelity`.
- Loop 18 visual/regression notes:
  - Texture-backed materials are objectively bound and visible in runtime receipts/counts, and the worst pale/black receiver-disc artifacts from the first Loop 18 candidates were reduced.
  - The final images are still `HUMAN-GATE` short of AAA: low-poly silhouettes, kit-like tent/cabin/props, stylized water/terrain, and visible overlay construction remain. The next front should climb another layer: source-bound hero geometry, better asset selection/ingest, or renderer-level occlusion/shadowing with a density budget rather than more decorative clutter.
- Loop 19 heartbeat proof:
  - `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa-loop19-proof --timeout 1 --poll 1` exited by timeout after 1s.
- Loop 19 red proof:
  - `aaa_graphics_campsite_loop18d`, `aaa_graphics_desert_loop18b`, and `aaa_graphics_alpine_loop18b` failed the strengthened graphics gate with only `missing_source_bound_hero_geometry`.
- Loop 19 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.source_geometry_fidelity` with source asset set, scanned lantern, utility prop, anchor rock, and hero-anchor counts.
  - `tools\scene_graphics_gate.py` now requires source-bound scanned hero mesh IR/runtime evidence for generated exteriors.
  - `src\Core\Engine_Scenes.cpp` now loads and places local scanned `Lantern_01`, `WoodenTable_01`, `Barrel_01`, and `boulder_01` meshes in generated exterior hero areas using the existing naturalistic PBR texture hook.
- Loop 19 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release rebuild exited 0 after compiling `Engine_Scenes.cpp`: `[OK] Build complete in 214.5s`.
  - `git diff --check` exited 0.
  - Campsite `aaa_graphics_campsite_loop19` rendered VALID; quality gate exited 0 (`purple_fraction=0.8841`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_fraction=0.0051`, `dark_contact_area_fraction=0.0326`); Director IR validation exited 0. Runtime log shows `source-bound hero geometry lanterns=2 utility_props=3 anchor_rocks=4 hero_anchors=9 source_sets=4`.
  - Desert canyon `aaa_graphics_desert_loop19` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4194`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_area_fraction=0.0827`); Director IR validation exited 0. Runtime log shows `source-bound hero geometry lanterns=2 utility_props=3 anchor_rocks=4 hero_anchors=9 source_sets=4`.
  - Alpine cabin `aaa_graphics_alpine_loop19` rendered VALID; quality gate exited 0 (`avg_luma=0.1225`, `cool_fraction=0.8607`, `nonblack_fraction=0.9956`); graphics gate exited 0 (`dark_contact_fraction=0.0222`); Director IR validation exited 0. Runtime log shows `source-bound hero geometry lanterns=2 utility_props=2 anchor_rocks=4 hero_anchors=8 source_sets=4`.
  - Kitchen smoke `regression_kitchen_aaa_loop19` rendered VALID and quality gate exited 0 (`avg_luma=0.3059`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports forbidden fridge, missing mountain/ridge, focal visibility, and purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with explicit empty log and `--expect-fail` exited 0 and now includes `missing_source_bound_hero_geometry`.
- Loop 19 visual/regression notes:
  - Source-scanned meshes are visible in the campsite/desert hero camp and on the alpine porch, and they use existing PBR texture paths instead of new procedural primitive overlays.
  - The remaining `HUMAN-GATE` gap is still large: base terrain/water/backdrop and many hero silhouettes remain stylized. The next front should target renderer-level shadow/occlusion budgets, asset-selection quality, or a stronger generated-scene source asset planner.
- Loop 20 opened after Loop 19 checkpoint. Local-loop check: source props help the close hero read, but the stills still lack convincing renderer-integrated shadowing and occlusion. Next action: strengthen the graphics gate so Loop 19 artifacts fail a renderer-level shadow/occlusion-budget requirement, then implement the narrowest runtime control/telemetry pass that satisfies it without forcing DXR defaults.
- Loop 20 heartbeat:
  - Previous `aaa-loop20` heartbeat had timed out after the long Release build; proof `aaa-loop20-proof` fired by timeout after 1s, and `aaa-loop20` was rearmed as a Codex serve heartbeat with PID `82588`.
- Loop 20 red proof:
  - `aaa_graphics_campsite_loop19`, `aaa_graphics_desert_loop19`, and `aaa_graphics_alpine_loop19` failed the strengthened graphics gate with only `missing_renderer_shadow_occlusion_budget`.
- Loop 20 implementation:
  - `tools\scene_graphics_gate.py` now requires `graphics_pass.renderer_shadow_occlusion_budget` plus parsed runtime readback for SSAO, shadow maps, shadow bias/PCF, bounded receiver-contact counts, and `dxr_required=0`.
  - `tools\scene_compiler.py` now emits a generated-exterior renderer shadow/occlusion budget tied to the same SSAO/shadow values used by the renderer path.
  - `src\Core\Engine_Scenes.cpp` now parses that budget, applies it through the existing renderer setters, reads back `GetFeatureState()`/`GetQualityState()`, and logs `generative_exterior: renderer shadow occlusion budget ...`.
- Loop 20 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release rebuild exited 0 after compiling and linking `Engine_Scenes.cpp`: `[OK] Build complete in 191.1s`.
  - `git diff --check` exited 0 with only line-ending warnings from the working tree.
  - Campsite `aaa_graphics_campsite_loop20` rendered VALID; quality gate exited 0 (`purple_fraction=0.8813`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_fraction=0.0051`, `dark_contact_area_fraction=0.0329`); Director IR validation exited 0. Runtime readback shows `ssao=on shadows=on ssao_radius=1.26 ssao_bias=0.018 ssao_intensity=2.70 shadow_bias=0.0020 shadow_pcf=3.10 contact_patches=56 soft_penumbra=32 overlay_budget=88 dxr_required=0`.
  - Desert canyon `aaa_graphics_desert_loop20` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4198`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_area_fraction=0.0829`); Director IR validation exited 0. Runtime readback shows the same non-DXR renderer shadow/SSAO budget with 56 receiver-contact patches and 32 soft penumbra patches.
  - Alpine cabin `aaa_graphics_alpine_loop20` rendered VALID; quality gate exited 0 (`avg_luma=0.122`, `cool_fraction=0.8606`, `nonblack_fraction=0.9957`); graphics gate exited 0 (`dark_contact_fraction=0.0222`); Director IR validation exited 0. Runtime readback shows `ssao=on shadows=on ssao_radius=1.18 ssao_bias=0.018 ssao_intensity=2.28 shadow_bias=0.0020 shadow_pcf=2.60 contact_patches=18 soft_penumbra=22 overlay_budget=40 dxr_required=0`.
  - Kitchen smoke `regression_kitchen_aaa_loop20` rendered VALID and quality gate exited 0 (`avg_luma=0.4697`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports forbidden fridge, missing mountain/ridge, focal visibility, and purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with an explicit empty runtime log and `--expect-fail` exited 0 and now includes `missing_renderer_shadow_occlusion_budget`.
- Loop 20 visual/regression notes:
  - Renderer-level SSAO/shadow settings are now separately contract-gated and runtime-readback verified. This is still a bounded SSAO/shadow-map/contact budget, not forced DXR/RT; final AAA quality remains `HUMAN-GATE`.
  - Remaining `HUMAN-GATE` gap: scene planning and asset staging still read kit-like in places. The next high-leverage front should target generated-scene asset planning/composition quality or terrain/water/backdrop fidelity, not another generic receipt.
- Loop 21 opened after visual inspection of Loop 20 outputs. Local-loop check: the next blocker is disconnected staging and repeated generic kits, not another renderer telemetry receipt. Retired `aaa-loop20`; heartbeat proof `aaa-loop21-proof` fired by timeout after 1s, and `aaa-loop21` is armed with PID `74184`. Next action: strengthen the graphics gate so Loop 20 artifacts fail a prompt-aware composition/staging fidelity requirement, then implement targeted compiler/runtime staging cleanup.
- Loop 17 opened after Loop 16 visual inspection and user pushback: the next hard ceiling is source/hero/environment geometry fidelity rather than more overlay layers. Heartbeat proof `aaa-graphics-loop17-proof` fired by timeout after 1s.
- Loop 17 red proof:
  - Strengthened `tools\scene_graphics_gate.py` to require `graphics_pass.hero_environment_geometry` plus runtime logs for high-detail camp/cabin kits, mountain massing, and irregular tree silhouettes where prompt-relevant.
  - Current Loop 16 campsite, desert, and alpine artifacts failed the strengthened gate with `missing_hero_environment_geometry` and no unrelated new failures.
- Loop 17 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.hero_environment_geometry` counts for high-detail camp/cabin kit pieces, mountain/cliff massing, shoreline props, irregular tree silhouettes, and support props.
  - `src\Core\Engine_Scenes.cpp` now parses that contract and builds bounded thick runtime geometry for camp gear/tent pieces, cabin log/rafter/porch/foundation/woodpile pieces, mountain/cliff massing, shoreline driftwood/stones, and irregular tree silhouettes.
  - `tools\scene_graphics_gate.py` now fails generated exteriors without the new IR/runtime evidence.
- Loop 17 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Final Release rebuild exited 0: `[OK] Build complete in 66.0s`.
  - Campsite `aaa_graphics_campsite_loop17b` rendered VALID; quality gate exited 0 (`purple_fraction=0.8847`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_area_fraction=0.0187`); Director IR validation exited 0. Runtime log shows `created high detail camp kit pieces=34`, `created mountain massing geometry layers=5 cliff_mass=0`, `created irregular tree silhouette geometry trees=12`, and `created hero environment geometry camp=34 cabin=0 mountain_layers=5 cliff_mass=0 shoreline_props=10 tree_silhouettes=12 support_props=12`.
  - Desert canyon `aaa_graphics_desert_loop17b` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4216`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_area_fraction=0.0069`); Director IR validation exited 0. Runtime log shows `created high detail camp kit pieces=34`, `created mountain massing geometry layers=5 cliff_mass=14`, and `created hero environment geometry camp=34 cabin=0 mountain_layers=5 cliff_mass=14 shoreline_props=10 tree_silhouettes=0 support_props=12`.
  - Alpine cabin `aaa_graphics_alpine_loop17` rendered VALID; quality gate exited 0 (`avg_luma=0.1499`, `cool_fraction=0.8591`, `nonblack_fraction=1.0`); graphics gate exited 0 (`dark_contact_fraction=0.0048`); Director IR validation exited 0. Runtime log shows `created high detail cabin kit pieces=30`, `created mountain massing geometry layers=5 cliff_mass=0`, `created irregular tree silhouette geometry trees=12`, and `created hero environment geometry camp=0 cabin=30 mountain_layers=5 cliff_mass=0 shoreline_props=10 tree_silhouettes=12 support_props=8`.
  - Kitchen smoke `regression_kitchen_aaa_loop17` rendered VALID and quality gate exited 0 (`avg_luma=0.4728`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports forbidden fridge, missing mountain/ridge, focal visibility, and purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with explicit empty log and `--expect-fail` exited 0 and now includes `missing_hero_environment_geometry`.
- Loop 17 visual/regression notes:
  - First `aaa_graphics_campsite_loop17` failed `purple_water_roi_fail` because non-canyon mountain massing became near-side walls in the water ROI. Fixed by pushing non-canyon massing outward/back and disabling non-canyon cliff chunks.
  - First `aaa_graphics_desert_loop17` failed `turquoise_water_roi_fail` because canyon massing narrowed the sampled river band. Fixed by shrinking and moving canyon Loop 17 massing to side/back detail while preserving the existing canyon-wall system.
  - Visual residual remains `HUMAN-GATE`: Loop 17 adds real detail and form, but outputs are still stylized and some edge silhouettes are awkward, especially in alpine. The next front should bind source setpieces/regions to runtime geometry or attack asset/material ingestion rather than add unstructured clutter.
- Heartbeat proof: `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa-graphics-proof --timeout 1 --poll 1` exited by timeout after 1s.
- Loop 1 green:
  - `python tools\scene_graphics_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\v3_campsite_ridge_test_0_ir.json --png build\bin\logs\v3_campsite_ridge_test_0.png --expect-fail` exited 0.
  - Required failures were present: `missing_terrain_relief`, `missing_contact_grounding`, `missing_material_pass`, `missing_runtime_graphics_evidence`.
  - Report path: `build\bin\logs\scene_graphics\a_foggy_mountain_campsite_beside_a_purple_lake_at_dawn\graphics_gate_report.json`.
- Black-render attribution:
  - Enhanced campsite initially produced `build\bin\logs\aaa_graphics_campsite_0.png` as a black frame and hit `Timed out waiting for command queue fence: expected=19, completed=18`.
  - Probe command `CORTEX_DISABLE_RT=1` + `tools\render_ir.ps1` on the same IR exited 0 in 3.7s and produced `build\bin\logs\aaa_graphics_campsite_rt_off_probe.png`.
  - Conclusion: validation capture's forced DXR/BLAS path was the crash trigger for dense generated exteriors; terrain/material/contact path rendered cleanly with SSAO/SSR/shadows.
- Loop 2/3 implementation:
  - `tools\scene_compiler.py` now emits `environment.ground.terrain`, `environment.graphics_pass`, per-object material hints, and bounded contact patches.
  - `src\Core\Engine_Scenes.cpp` now builds a procedural heightfield terrain mesh, shore/contact grounding layers, and runtime graphics evidence logs for renderer quality, terrain, contact, and materials.
  - `src\LLM\SceneRecipes.cpp`, `src\LLM\SceneCommands.h`, and `src\LLM\CommandQueue.cpp` now lower IR material overrides including normal/specular controls.
  - `tools\render_ir.ps1` disables DXR for generated captures by default, with `CORTEX_ENABLE_GENERATIVE_DXR=1` as the opt-in.
- Verifier commands:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build via heartbeat-guarded background lane exited 0: `[OK] Build complete in 98.8s`.
  - `python tools\scene_gen.py "a foggy mountain campsite beside a purple lake at dawn" --no-critic --name aaa_graphics_campsite_v2 --fast` exited 0 with `backend=director_v3`, `objects=54`, `VALID`.
  - Campsite quality gate exited 0: purple ROI `purple_fraction=0.8855`, frame `nonblack_fraction=1.0`.
  - Campsite graphics gate exited 0 with runtime terrain/contact/material evidence; warning remained `weak_image_contact_metric`.
  - `python tools\scene_gen.py "a stormy alpine lake with a small cabin and blue moonlight" --no-critic --name aaa_graphics_alpine --fast` exited 0 with `backend=director_v3`, `objects=50`, `VALID`.
  - Alpine quality gate on `aaa_graphics_alpine_gate.png` exited 0: frame `avg_luma=0.2675`, `cool_fraction=0.8859`.
  - Alpine graphics gate exited 0 with image contact metric `dark_contact_fraction=0.0068`.
  - `python tools\scene_gen.py "a sunny desert canyon campsite with red rocks and a turquoise river" --no-critic --name aaa_graphics_desert --fast` exited 0 with `backend=director_v3`, `objects=54`, `VALID`.
  - Desert quality gate on `aaa_graphics_desert_gate.png` exited 0: turquoise ROI `turquoise_fraction=0.3823`.
  - Desert graphics gate exited 0 with warning `weak_image_contact_metric`.
  - Known-bad quality gate with `--expect-fail` exited 0 and still reported `forbidden_asset_class`, `missing_prompt_entity`, `focal_visibility_fail`, and `purple_water_roi_fail`.
  - Known-bad graphics gate with `--expect-fail` exited 0 and still reported the required graphics failure codes.
  - `python tools\director_ir_v3.py --validate build\bin\logs\aaa_graphics_alpine_director_v3.json` exited 0.
  - `python tools\director_ir_v3.py --validate build\bin\logs\aaa_graphics_desert_director_v3.json` exited 0.
  - `python tools\scene_gen.py "a cozy kitchen with a wooden table and plants" --no-critic --name regression_kitchen_aaa --fast` exited 0 with `backend=codex`, `setting=interior`, `VALID`.
  - Kitchen quality gate exited 0 with `avg_luma=0.263`, `nonblack_fraction=1.0`.
- Reopened after user rejected the baseline as still too basic/disconnected.
- Heartbeat proof for the reopened loop: `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa-graphics-loop5-proof --timeout 1 --poll 1` exited by timeout after 1s.
- Loop 5 red/green verifier proof:
  - `python tools\scene_graphics_gate.py --prompt "a sunny desert canyon campsite with red rocks and a turquoise river" --ir build\bin\logs\aaa_graphics_desert_0_ir.json --png build\bin\logs\aaa_graphics_desert_gate.png --log build\bin\logs\aaa_graphics_desert_gate.out` exited 1.
  - Required structural failures were present: `insufficient_material_zone_variation`, `missing_world_depth_geometry`, `desert_canyon_blockout`, `tree_heavy_desert_staging`.
  - Report path: `build\bin\logs\scene_graphics\a_sunny_desert_canyon_campsite_with_red_rocks_and_a_turq\graphics_gate_report.json`.
- Loop 6/7/8 continuation after the user rejected the checkpoint as underscoped:
  - Proved Loop 7 gate red on `build\bin\logs\aaa_graphics_campsite_loop6_0_ir.json`: `missing_advanced_shader_materials`, `missing_occlusion_surface_layers`.
  - Added compiler/runtime support for advanced material terms (`ao`, `clearcoat`, `sheen`, `subsurface`, `anisotropy`, wetness/procedural masks), stronger contact shadows, occlusion ribbons, terrain creases, micro pebbles, shore foam, wet glints, and stable runtime logs.
  - Proved Loop 8 gate red on `build\bin\logs\aaa_graphics_campsite_loop7_0_ir.json`: `missing_shot_camera_pass`.
  - Added adaptive generated-exterior camera profiles: `closer_midground_hero` for camp/desert hero props and `balanced_cabin_hero` for generated cabin structures.
  - Heartbeat proofs exited by timeout as expected: `aaa-graphics-loop7-proof`, `aaa-graphics-loop8-proof`, `aaa-graphics-occlusion-proof`, `aaa-graphics-adaptive-camera-proof`.
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0 after the material/occlusion pass and again after adaptive camera: final `[OK] Build complete in 17.2s`.
  - `python tools\scene_gen.py "a foggy mountain campsite beside a purple lake at dawn" --no-critic --name aaa_graphics_campsite_loop9 --fast` exited 0, `objects=54`, `VALID`.
  - Campsite quality gate exited 0: purple ROI `purple_fraction=0.7842`, `nonblack_fraction=1.0`.
  - Campsite graphics gate exited 0 with runtime shader/material/occlusion/surface/camera evidence; residual warning `weak_image_contact_metric`.
  - `python tools\scene_gen.py "a sunny desert canyon campsite with red rocks and a turquoise river" --no-critic --name aaa_graphics_desert_loop9 --fast` exited 0, `objects=45`, `VALID`.
  - Desert quality gate exited 0: turquoise ROI `turquoise_fraction=0.4494`.
  - Desert graphics gate exited 0 with canyon walls/strata, shader/material, occlusion/surface, and closer camera evidence; residual warning `weak_image_contact_metric`.
  - `python tools\scene_gen.py "a stormy alpine lake with a small cabin and blue moonlight" --no-critic --name aaa_graphics_alpine_loop10 --fast` exited 0, `objects=50`, `VALID`.
  - Alpine quality gate exited 0: `cool_fraction=0.8818`, `nonblack_fraction=1.0`.
  - Alpine graphics gate exited 0 with no warnings; `dark_contact_fraction=0.009`.
  - Director IR validation exited 0 for `aaa_graphics_campsite_loop9_director_v3.json`, `aaa_graphics_desert_loop9_director_v3.json`, and `aaa_graphics_alpine_loop10_director_v3.json`.
  - Known-bad quality oracle is the original user-bad artifact, not the later v3 graphics fixture: `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\gen_a_foggy_mountain_campsite_beside_0_ir.json --png build\bin\logs\gen_a_foggy_mountain_campsite_beside_0.png --expect-fail` exited 0 with `forbidden_asset_class`, `missing_prompt_entity`, `focal_visibility_fail`, `purple_water_roi_fail`.
  - Known-bad graphics gate on `v3_campsite_ridge_test_0` with `--expect-fail` exited 0.
  - Kitchen smoke `python tools\scene_gen.py "a cozy kitchen with a wooden table and plants" --no-critic --name regression_kitchen_aaa_loop9 --fast` exited 0 with `backend=codex`, `setting=interior`, `VALID`; kitchen quality gate exited 0 with `avg_luma=0.278`, `nonblack_fraction=1.0`.

## Learnings

- 2026-07-03 Loop 21 pivot: the previous composition/staging path was killed before implementation because it still optimized the existing scatter-based generator. The active loop now targets a structural source-authored scene-module overhaul: family-specific campsite lake, desert canyon river, and alpine cabin lake modules with cohesive setpieces and authored lighting. This loop must prove old Loop 20 artifacts fail a new module contract before runtime code is accepted.
- Loop 21 source-authored module checkpoint:
  - Red proof: Loop 20 campsite/desert/alpine artifacts fail `scene_graphics_gate.py` with `missing_authored_scene_module`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.authored_scene_module`; `src/Core/Engine_Scenes.cpp` parses it, applies module-specific lighting, builds cohesive terrain/shore/backdrop/hero setpieces, and logs runtime receipts; `tools/scene_graphics_gate.py` requires the IR/runtime module evidence.
  - Final green evidence: Release build `[OK] Build complete in 35.3s`; `aaa_module_campsite_loop21e`, `aaa_module_desert_loop21e`, and `aaa_module_alpine_loop21e` are VALID and pass quality, graphics, and Director validation gates; known-bad quality/graphics oracles still fail; kitchen smoke `regression_kitchen_aaa_loop21e` quality green.
  - Visual residual: the module pass improves purple water, authored lighting, cabin moonlight/warm windows, and canyon/campsite composition, but the assets still read low-poly and kit-like. Next loop should attack source asset replacement/material shader integration, not add another overlay/detail metric.
- Loop 22 contract:
  - Invariant: generated exteriors must have integrated material relief, source-texture/triplanar detail, localized shadow casters/receivers, and cinematic light/fog slices tied to the authored module.
  - Entry: Loop 21 artifacts exist and current build is green on `main` at `98e434f`.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, broad asset-import rewrites, parallel render validation.
  - Verifier: first make `scene_graphics_gate.py` reject Loop 21 campsite/desert/alpine with `missing_cinematic_material_lighting_pass`; then require new IR/runtime evidence and run Release build, Python compile, sequential campsite/desert/alpine renders, quality gates, graphics gates, Director validation, known-bad oracles, and kitchen smoke.
  - Exit: verifier green on novel prompts plus visual inspection confirms visible material/shadow/light improvement; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if the pass becomes disconnected clutter, produces obvious dark stains/light cards, regresses water/color/focal gates, or requires forced DXR.
- Loop 22 verifier evidence:
  - Red proof: `aaa_module_campsite_loop21e`, `aaa_module_desert_loop21e`, and `aaa_module_alpine_loop21e` failed the strengthened graphics gate with only `missing_cinematic_material_lighting_pass`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.cinematic_material_lighting`; `tools/scene_graphics_gate.py` requires IR plus runtime log evidence; `src/Core/Engine_Scenes.cpp` parses and renders terrain-toned relief patches, localized shadow caster/receiver geometry, wet/dry shore roughness variation, subtle fog/light slices, and shadow-casting local lights.
  - Artifact correction: the first `loop22` renders had obvious bright bands and black caster blocks. The final `loop22c` pass moved fog slices to far flanks, reduced opacity, shrank caster geometry, and removed texture re-tinting from overlay strips.
  - Build/regression: Python compile green; Release rebuild green (`[OK] Build complete in 21.9s` final); `git diff --check` green except existing CRLF warnings.
  - Final campsite `aaa_cinematic_campsite_loop22c` quality green (`purple_fraction=0.8835`), graphics green (`dark_contact_fraction=0.0212`, `dark_contact_area_fraction=0.2711`), Director validation green, runtime `triplanar_layers=7 relief_patches=30 shadow_casters=12 contact_receivers=30 localized_lights=3 volumetric_slices=6 wet_variation=12`.
  - Final desert `aaa_cinematic_desert_loop22c` quality green (`turquoise_fraction=0.4195`), graphics green (`dark_contact_area_fraction=0.1247`), Director validation green, runtime `triplanar_layers=8 relief_patches=34 shadow_casters=12 contact_receivers=30 localized_lights=2 volumetric_slices=6 wet_variation=12`.
  - Final alpine `aaa_cinematic_alpine_loop22c` quality green (`avg_luma=0.1221`, `cool_fraction=0.889`), graphics green (`dark_contact_fraction=0.0199`, `dark_contact_area_fraction=0.7739`), Director validation green, runtime `triplanar_layers=7 relief_patches=26 shadow_casters=9 contact_receivers=20 localized_lights=2 volumetric_slices=6 wet_variation=12`.
  - Kitchen smoke `regression_kitchen_aaa_loop22` quality green (`avg_luma=0.479`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` still fails with the fridge/missing-ridge/focal/purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` still fails and now includes `missing_cinematic_material_lighting_pass`.
  - Visual residual: this pass improves material/contact depth and removes the first-pass artifacts, but the outputs are still not AAA. The next loop must attack hero asset replacement/source silhouette quality, especially the low-poly orange tent and simple camp/cabin props.
- Loop 23 contract:
  - Invariant: generated exteriors must replace or visibly overbuild the dominant low-poly hero silhouettes instead of merely decorating them.
  - Entry: Loop 22 artifacts exist and current build is green on `main` at `0a6b567`.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, parallel render validation, broad asset-import rewrites.
  - Verifier: first make `scene_graphics_gate.py` reject Loop 22 campsite/desert/alpine with `missing_hero_asset_replacement`; then require new IR/runtime evidence and run Python compile, Release build, sequential campsite/desert/alpine renders, quality gates, graphics gates, Director validation, known-bad oracles, and kitchen smoke.
  - Exit: verifier green on novel prompts plus visual inspection confirms the tent/cabin/canyon dominant shapes are materially changed; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if replacement geometry becomes obvious blocking cards, hides water/color/focal subjects, destabilizes captures, or just adds more disconnected clutter.
- Loop 23 verifier evidence:
  - Heartbeat proof: `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa-loop23-proof --timeout 1 --poll 1` fired by timeout after 1s.
  - Red proof: `aaa_cinematic_campsite_loop22c`, `aaa_cinematic_desert_loop22c`, and `aaa_cinematic_alpine_loop22c` fail the strengthened graphics gate with only `missing_hero_asset_replacement`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.hero_asset_replacement`; `tools/scene_graphics_gate.py` requires IR/runtime evidence; `src\Core\Engine_Scenes.cpp` builds a dominant canvas tent shell with fabric layers/poles/ropes/masking panels, cabin facade/roof/deck/foundation overbuild, and canyon hero rock masses.
  - Visual correction: first `loop23` artifacts passed gates but the tent replacement read as a flat black triangle. Final `loop23b` increased canvas albedo and AO response before checkpointing.
  - Build/regression: Python compile green; final Release rebuild green (`[OK] Build complete in 40.3s`); `git diff --check` green except CRLF warnings.
  - Final campsite `aaa_hero_replace_campsite_loop23b` quality green (`purple_fraction=0.8792`), graphics green (`dark_contact_fraction=0.023`, `dark_contact_area_fraction=0.341`), Director validation green, runtime `canvas_shell=16 fabric_layers=18 structural_poles=10 rope_stakes=14 low_poly_masks=5 cabin_facade=0 cabin_roof=0 cabin_deck_foundation=0 hero_rock_masses=0`.
  - Final desert `aaa_hero_replace_desert_loop23b` quality green (`turquoise_fraction=0.4158`), graphics green (`dark_contact_area_fraction=0.189`), Director validation green, runtime `canvas_shell=16 fabric_layers=18 structural_poles=10 rope_stakes=14 low_poly_masks=5 hero_rock_masses=12`.
  - Final alpine `aaa_hero_replace_alpine_loop23b` quality green (`avg_luma=0.1164`, `cool_fraction=0.9058`), graphics green (`dark_contact_fraction=0.0246`, `dark_contact_area_fraction=0.801`), Director validation green, runtime `cabin_facade=18 cabin_roof=10 cabin_deck_foundation=12`.
  - Kitchen smoke `regression_kitchen_aaa_loop23b` quality green (`avg_luma=0.4177`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` still fails with the fridge/missing-ridge/focal/purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` still fails and now includes `missing_hero_asset_replacement`.
  - Visual residual: the replacement pass changes the dominant tent/cabin forms, but the campsite/desert frames still read cluttered, disconnected, and game-kit-like. Next loop should attack cohesive staging/pruning and shot composition rather than adding more detail geometry.
- Loop 24 contract:
  - Invariant: generated exteriors must cluster camp/cabin props around coherent hero axes and keep foreground dressing out of the central sightline instead of scattering detail across the frame.
  - Entry: Loop 23 checkpoint is committed at `8338e8a` and exposes cluttered `loop23b` artifacts.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers.
  - Scope out: weakening `tools/scene_quality_gate.py`, deleting existing graphics gates, forcing DXR, parallel render validation.
  - Verifier: first make `scene_graphics_gate.py` reject Loop 23b campsite/desert/alpine with `missing_cohesive_staging_cleanup`; then require new IR/runtime evidence and run Python compile, Release build, sequential campsite/desert/alpine renders, quality gates, graphics gates, Director validation, known-bad oracles, and kitchen smoke.
  - Exit: verifier green plus visual inspection shows fewer center-lane loose props and clearer hero staging; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if cleanup merely adds cover cards, hides prompt-critical water/hero objects, or breaks previous geometry/material/occlusion gates.
- Loop 24 verifier evidence:
  - Heartbeat proof: `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa-loop24-proof --timeout 1 --poll 1` fired by timeout after 1s.
  - Red proof: `aaa_hero_replace_campsite_loop23b`, `aaa_hero_replace_desert_loop23b`, and `aaa_hero_replace_alpine_loop23b` fail the strengthened graphics gate with only `missing_cohesive_staging_cleanup`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.cohesive_staging_cleanup`; `tools/scene_graphics_gate.py` requires IR/runtime evidence; `src\Core\Engine_Scenes.cpp` pushes foreground frames/twigs to the side lanes, shrinks/splits the campsite pad, clusters bedroll/log-seat/lantern pieces around the tent/fire axis, and logs `generative_exterior: cohesive staging cleanup`.
  - Takeover heartbeat proof: `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label codex-aaa-proof --timeout 1 --poll 1` fired by timeout after 1s.
  - Build/regression: `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0; `git diff --check` exited 0 except CRLF warnings; Release rebuild exited 0 (`[OK] Build complete in 3.5s`, no work needed).
  - Final campsite `aaa_cohesive_campsite_loop24c` quality green (`purple_fraction=0.8788`, `nonblack_fraction=0.9843`), graphics green (`dark_contact_fraction=0.0197`, `dark_contact_area_fraction=0.3283`), Director validation green, runtime cleanup receipt present.
  - Final desert `aaa_cohesive_desert_loop24c` quality green (`turquoise_fraction=0.4152`, `nonblack_fraction=0.9931`), graphics green (`dark_contact_fraction=0.0095`, `dark_contact_area_fraction=0.1778`), Director validation green, runtime cleanup receipt present.
  - Final alpine `aaa_cohesive_alpine_loop24c` quality green (`avg_luma=0.0925`, `cool_fraction=0.8882`, `nonblack_fraction=0.7576`), graphics green (`dark_contact_fraction=0.0365`, `dark_contact_area_fraction=0.8824`), Director validation green, runtime cleanup receipt present.
  - Kitchen smoke `regression_kitchen_aaa_loop24` rendered VALID and quality gate exited 0 (`avg_luma=0.4479`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` still reports forbidden fridge assets, missing ridge, focal visibility, and purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` still fails and now includes `missing_cohesive_staging_cleanup`.
  - Visual inspection: the pass reduces some central clutter and removes desert grass scatter, but it does not solve the user's actual fidelity complaint. The latest stills remain flat, stylized, low-poly, and stage-like. Next loop must attack environment/lighting/material integration at a larger structural layer: sky/atmosphere, water depth/reflection bands, terrain relief, directional shadows, and backdrop integration.
- Loop 25 contract:
  - Invariant: generated exteriors must stop reading as flat stage planes; sky/atmosphere, water, terrain, backdrop, and directional light/shadow must be integrated as one environment pass.
  - Entry: Loop 24 checkpoint is green, but `aaa_cohesive_*_loop24c` artifacts are visually rejected as flat/stylized despite passing objective gates.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers, novel generated exterior artifacts.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, adding disconnected prop clutter, parallel render validation.
  - Verifier: first make `scene_graphics_gate.py` reject Loop 24 campsite/desert/alpine artifacts with `missing_environment_fidelity_pass`; then require new IR/runtime evidence and run Python compile, Release build, sequential campsite/desert/alpine renders, quality gates, graphics gates, Director validation, known-bad oracles, and kitchen smoke.
  - Exit: verifier green plus visual inspection shows less empty gray sky/hard horizon/flat water-plane staging and stronger directional light/shadow read; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if the implementation creates visible light cards, black bands/puddles, covers water/color ROIs, or only adds metadata without a visible structural change.
- Loop 25 verifier evidence:
  - Red proof: with only the verifier edit, `aaa_cohesive_campsite_loop24c`, `aaa_cohesive_desert_loop24c`, and `aaa_cohesive_alpine_loop24c` all failed with exactly `missing_environment_fidelity_pass`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.environment_fidelity` and aligns renderer SSAO/shadow budgets with the environment pass; `tools/scene_graphics_gate.py` requires IR/runtime environment evidence; `src\Core\Engine_Scenes.cpp` parses the contract, adjusts sky presentation/IBL/fog/SSAO/shadow/water optics, and creates low-alpha plane-based terrain, water-depth, reflection, and directional-shadow integration geometry.
  - Visual correction: first `loop25` artifacts were rejected because sky/horizon/backdrop overlay cubes produced obvious horizontal strip/card artifacts. Final `loop25c` removes visible sky/horizon/backdrop cards and keeps those parts as renderer/fog controls plus runtime receipts.
  - Build/regression: Python compile exited 0; Release rebuild exited 0 (`[OK] Build complete in 37.1s`, final no-work rebuild `[OK] Build complete in 3.2s`); `git diff --check` exited 0 except CRLF warnings.
  - Final campsite `aaa_environment_campsite_loop25c` rendered VALID; quality green (`purple_fraction=0.878`, `nonblack_fraction=0.9868`); graphics green (`dark_contact_fraction=0.0192`, `dark_contact_area_fraction=0.2973`); Director validation green.
  - Final desert `aaa_environment_desert_loop25c` rendered VALID; quality green (`turquoise_fraction=0.4117`, `nonblack_fraction=0.9933`); graphics green (`dark_contact_fraction=0.0092`, `dark_contact_area_fraction=0.1897`); Director validation green.
  - Final alpine `aaa_environment_alpine_loop25c` rendered VALID; quality green (`avg_luma=0.1301`, `cool_fraction=0.9088`, `nonblack_fraction=0.9325`); graphics green (`dark_contact_fraction=0.0283`, `dark_contact_area_fraction=0.7362`); Director validation green.
  - Kitchen smoke `regression_kitchen_aaa_loop25` rendered VALID and quality gate exited 0 (`avg_luma=0.2991`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` still reports forbidden fridge assets, missing ridge, focal visibility, and purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` still fails and now includes `missing_environment_fidelity_pass`.
  - Visual inspection: the artifact-fixed environment pass does not make the scene AAA. It improves renderer/environment contract coverage and avoids the first strip failure, but low-poly mountains/canyon slabs, flat water, and kit silhouettes remain dominant. Next loop should replace the environment/backdrop source quality, not add another overlay layer.
- Loop 26 contract:
  - Invariant: generated exteriors must replace the low-poly stage-like mountain/canyon/backdrop read with source-bound environment assets and stronger terrain/backdrop composition.
  - Entry: Loop 25 checkpoint is green, but the `loop25c` artifacts remain visibly stylized due to source/catalog terrain and backdrop quality.
  - Scope in: asset inventory, `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers, novel generated exterior artifacts.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, hiding water/color ROIs, adding unrelated prop clutter, parallel render validation.
  - Verifier: inventory suitable local source assets; strengthen `scene_graphics_gate.py` so Loop 25 campsite/desert/alpine fail `missing_source_environment_assets`; then require new IR/runtime source-environment evidence and run the standard build/render/gate/oracle suite.
  - Exit: verifier green plus visual inspection shows the low-poly backdrop/source terrain read is reduced without new strip/card artifacts; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if suitable assets are not present locally, if placement worsens prompt semantics, or if the pass becomes disconnected clutter instead of backdrop/source replacement.
- Loop 26 verifier evidence:
  - Heartbeat proof `aaa-loop26-proof` fired by timeout after 1s.
  - Asset inventory confirmed local source meshes: `naturalistic_showcase/boulder_01`, `rock_moss_set_01`, `fetched/gen_weathered_grey_boulder`, `gen_jagged_volcanic_monolith`, `gen_battery_test_crag_stone`, Kenney `cliff_large_rock`, `cliff_cornerLarge_rock`, `cliff_blockSlope_rock`, and detailed pine meshes.
  - Red proof: `aaa_environment_campsite_loop25c`, `aaa_environment_desert_loop25c`, and `aaa_environment_alpine_loop25c` all failed the strengthened graphics gate with exactly `missing_source_environment_assets`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.source_environment_assets`; `tools/scene_graphics_gate.py` requires IR plus runtime source-environment receipts; `src/Core/Engine_Scenes.cpp` parses the contract, loads fetched/Kenney/naturalistic source meshes, places them in water-ROI-preserving flank/backdrop lanes, and logs source counts.
  - Python compile exited 0.
  - Release build exited 0: `[OK] Build complete in 57.5s`; final no-work rebuild exited 0: `[OK] Build complete in 3.0s`.
  - `git diff --check` exited 0 except expected CRLF warnings.
  - Campsite `aaa_source_env_campsite_loop26` rendered VALID; quality green (`purple_fraction=0.8101`, `nonblack_fraction=0.9871`); graphics green (`dark_contact_area_fraction=0.2982`); Director validation green; runtime `fetched_rocks=9 kenney_cliffs=4 detailed_trees=9 naturalistic_anchors=7 terrain_replacements=5 backdrop_anchors=27 source_sets=10`.
  - Desert `aaa_source_env_desert_loop26` rendered VALID; quality green (`turquoise_fraction=0.389`, `nonblack_fraction=0.9442`); graphics green (`dark_contact_area_fraction=0.3553`); Director validation green; runtime `fetched_rocks=12 kenney_cliffs=8 detailed_trees=0 naturalistic_anchors=7 terrain_replacements=6 backdrop_anchors=26 source_sets=10`.
  - Alpine `aaa_source_env_alpine_loop26` rendered VALID; quality green (`avg_luma=0.1254`, `cool_fraction=0.9087`, `nonblack_fraction=0.9326`); graphics green (`dark_contact_area_fraction=0.7362`); Director validation green; runtime `fetched_rocks=9 kenney_cliffs=4 detailed_trees=9 naturalistic_anchors=7 terrain_replacements=5 backdrop_anchors=27 source_sets=10`.
  - Kitchen smoke `regression_kitchen_aaa_loop26` rendered VALID and quality gate exited 0 (`avg_luma=0.4692`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` still reports forbidden fridge assets, missing ridge, focal visibility, and purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` still fails and now includes `missing_source_environment_assets`.
  - Visual inspection: source flank/backdrop assets are visible and do not block the violet/turquoise/cool ROIs or introduce strip/card artifacts. Residual remains large: scenes still read stylized, dark/flat around hero surfaces, and kit-like. Next loop should target hero material/shadow readability rather than adding more scatter.
- Loop 27 contract:
  - Invariant: generated exteriors must stop presenting dominant hero surfaces as crushed black/flat kit silhouettes; tent/cabin/canyon hero regions need readable material response, directional fill/rim shaping, and contact shadow coherence without global washout.
  - Entry: Loop 26 checkpoint is green, but visual inspection shows the tent, cabin, and canyon hero faces remain too flat/dark and disconnected from lighting/material detail.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers, novel generated exterior artifacts.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, global exposure-only fixes, visible light cards, hiding water/color ROIs, unrelated interior scenes.
  - Verifier: strengthen `scene_graphics_gate.py` so Loop 26 campsite/desert/alpine fail `missing_hero_material_shadow_readability`; then require new IR/runtime readability evidence and run the standard build/render/gate/oracle suite.
  - Exit: verifier green plus visual inspection shows less black-sheet tent/cabin/canyon read and stronger local material/shadow separation; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if the pass washes out night/desert/campsite color gates, creates visible glow cards/bands, or only changes metadata without visible hero-region improvement.
- Loop 27 verifier evidence:
  - Heartbeat proof `aaa-loop27-resume-proof` fired by timeout after 1s on takeover.
  - Red proof was established before implementation: Loop 26 campsite/desert/alpine artifacts failed the strengthened graphics gate with `missing_hero_material_shadow_readability`.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.hero_material_shadow_readability` and brighter fabric material hints; `tools/scene_graphics_gate.py` requires IR plus runtime hero-readability receipts; `src/Core/Engine_Scenes.cpp` parses the contract and adds hero material panels, shadow receivers, local fill lights, rim lights, and bounded exposure/SSAO/shadow PCF shaping.
  - Python compile exited 0.
  - Release rebuild exited 0: `[OK] Build complete in 4.6s`; final no-work rebuild exited 0: `[OK] Build complete in 3.7s`.
  - `git diff --check` exited 0 except expected CRLF warnings.
  - Campsite `aaa_hero_read_campsite_loop27e` quality green (`purple_fraction=0.809`, `avg_luma=0.341`, `nonblack_fraction=0.9901`); graphics green (`dark_contact_area_fraction=0.2404`); Director validation green.
  - Desert `aaa_hero_read_desert_loop27` quality green (`turquoise_fraction=0.3804`, `avg_luma=0.3144`, `nonblack_fraction=0.996`); graphics green (`dark_contact_area_fraction=0.1024`); Director validation green.
  - Alpine `aaa_hero_read_alpine_loop27` quality green (`avg_luma=0.1297`, `cool_fraction=0.906`, `nonblack_fraction=0.9431`); graphics green (`dark_contact_area_fraction=0.7099`); Director validation green.
  - Kitchen smoke `regression_kitchen_aaa_loop27` rendered VALID and quality green (`avg_luma=0.4502`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` still reports forbidden fridge assets, missing ridge, focal visibility, and purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` still fails and now includes `missing_hero_material_shadow_readability`.
  - Visual inspection: Loop 27 is an improvement, not a solution. The campsite/desert tent is less black than earlier attempts and the alpine cabin has better material/window separation, but all three still read as stage-like, low-poly, and disconnected. The next loop must attack structural world/hero fidelity and material/shadow presentation at a deeper layer.
- Loop 28 contract:
  - Invariant: generated exteriors must stop reading as props on a flat stage sheet; the ground/shore/hero interface needs high-tessellation displaced terrain, integrated foundations, material blend geometry, and actual shadow-casting structural forms.
  - Entry: Loop 27 checkpoint is green, but visual inspection shows the terrain is still planar-looking and dominant tent/cabin/canyon forms sit on top of the scene instead of being embedded in it.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers, novel generated exterior artifacts.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, hiding water/color ROIs, visible light cards/horizon strips, unrelated interior scenes, prompt-specific hacks.
  - Verifier: strengthen `scene_graphics_gate.py` so Loop 27 campsite/desert/alpine fail only `missing_structural_scene_fidelity`; then require `graphics_pass.structural_scene_fidelity`, raised terrain tessellation/relief, and runtime evidence for terrain tiles, displacement layers, hero foundations, material blends, light volumes, shadow casters, and shore segments.
  - Exit: verifier green plus visual inspection shows less flat-sheet terrain and better embedded hero/shore contact; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if the pass becomes more disconnected clutter, creates obvious strip/card artifacts, breaks purple/turquoise/cool water gates, or destabilizes generated captures.
- Loop 28 red proof:
  - Heartbeat proof `aaa-loop28-proof` fired by timeout after 1s.
  - `aaa_hero_read_campsite_loop27e`, `aaa_hero_read_desert_loop27`, and `aaa_hero_read_alpine_loop27` all failed the strengthened graphics gate with only `missing_structural_scene_fidelity`.
  - The red failure details show the current ceiling directly: terrain grid 72, relief `0.42/0.30/0.42`, micro relief `0.075/0.045/0.075`, and zero structural terrain tiles/foundations/material blends/light volumes/shore segments in IR/runtime.
- Loop 28 verifier evidence:
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.structural_scene_fidelity`, raises terrain grid to 96, and increases terrain relief/micro relief; `tools/scene_graphics_gate.py` requires the new IR/runtime contract; `src/Core/Engine_Scenes.cpp` parses the contract, drives the actual terrain generator, and creates structural terrain patches, hero foundations, low shadow casters, material blend patches, local lights, and shore bank segments.
  - Visual refinement before checkpoint: first render `aaa_structural_campsite_loop28` exposed rectangular foreground terrain cards and upright black posts. The pass was corrected with an irregular radial terrain patch mesh, low/angled shadow casters, and reduced material/shore patch visual weight before final validation.
  - Python compile exited 0.
  - Release build exited 0: `[OK] Build complete in 49.4s`; final no-work rebuild exited 0: `[OK] Build complete in 3.3s`.
  - `git diff --check` exited 0 except expected CRLF warnings.
  - Campsite `aaa_structural_campsite_loop28f` rendered VALID; quality green (`purple_fraction=0.8003`, `avg_luma=0.3399`, `nonblack_fraction=0.9884`); graphics green (`dark_contact_area_fraction=0.2515`); Director validation green; runtime `terrain_tiles=18 displacement_layers=5 hero_foundations=18 shadow_casters=14 material_blends=24 light_volumes=3 shore_segments=10 terrain_grid=96 relief=0.64`.
  - Desert `aaa_structural_desert_loop28f` rendered VALID; quality green (`turquoise_fraction=0.3802`, `avg_luma=0.3159`, `nonblack_fraction=0.9954`); graphics green (`dark_contact_area_fraction=0.1176`); Director validation green; runtime `terrain_tiles=18 displacement_layers=4 hero_foundations=18 shadow_casters=14 material_blends=18 light_volumes=3 shore_segments=10 terrain_grid=96 relief=0.48`.
  - Alpine `aaa_structural_alpine_loop28f` rendered VALID; quality green (`avg_luma=0.1304`, `cool_fraction=0.9039`, `nonblack_fraction=0.9413`); graphics green (`dark_contact_area_fraction=0.7054`); Director validation green; runtime `terrain_tiles=18 displacement_layers=5 hero_foundations=16 shadow_casters=10 material_blends=24 light_volumes=2 shore_segments=10 terrain_grid=96 relief=0.56`.
  - Kitchen smoke `regression_kitchen_aaa_loop28` rendered VALID and quality green (`avg_luma=0.3066`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` still reports forbidden fridge assets, missing ridge, focal visibility, and purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` still fails and now includes `missing_structural_scene_fidelity`.
  - Visual inspection: Loop 28 is a real structural step, not final AAA. It reduces the completely flat-stage feel and improves hero grounding, but campsite/desert/alpine still read as stylized kit scenes with clutter, simplified hero meshes, and limited true material/shadow realism. The next loop should target the actual asset/material renderer ceiling rather than adding more patch geometry.
- Loop 29 contract:
  - Invariant: generated exteriors must reduce the dominant kit-hero silhouette and weak PBR read by rebuilding prompt-family hero forms with procedural shells, cabin cladding/roof layers, canyon meshes, layered material terms, and low-poly silhouette masks.
  - Entry: Loop 28 checkpoint is green, but visual inspection shows the scene is still dominated by simple tent/cabin/canyon hero forms and weak material-light response.
  - Scope in: `tools/scene_compiler.py`, `tools/scene_graphics_gate.py`, `src/Core/Engine_Scenes.cpp`, campaign ledgers, novel generated exterior artifacts.
  - Scope out: weakening `tools/scene_quality_gate.py`, forcing generated DXR by default, hiding water/color ROIs, adding visible light cards, unrelated interior scenes, unbounded mesh density, or prompt-specific hacks.
  - Verifier: strengthen `scene_graphics_gate.py` so Loop 28 campsite/desert/alpine fail `missing_hero_mesh_material_overhaul`; then require IR/runtime evidence for rebuilt tent shell, cabin cladding, roof layers, canyon hero meshes, PBR layers, and silhouette masks.
  - Exit: verifier green plus visual inspection shows improved dominant hero material/silhouette readability without new side-wall/card artifacts; final AAA call remains `HUMAN-GATE`.
  - Escape: stop if the pass creates more disconnected clutter, panel-like canyon walls, crushed roofs, semantic color ROI failures, or metadata-only changes.
- Loop 29 verifier evidence:
  - Heartbeat proof `aaa-loop29-codex-proof` fired by timeout after 1s.
  - Red proof: Loop 28 final campsite/desert/alpine artifacts failed the strengthened graphics gate with `missing_hero_mesh_material_overhaul` before runtime implementation.
  - Implementation: `tools/scene_compiler.py` emits `graphics_pass.hero_mesh_material_overhaul` and new hero material zones; `tools/scene_graphics_gate.py` requires IR/runtime hero-mesh material receipts; `src/Core/Engine_Scenes.cpp` parses the contract and creates A-frame canvas shell/PBR strips, cabin cladding/roof strips, canyon hero meshes, and low-poly silhouette masks with texture/PBR material hooks.
  - Visual refinement before checkpoint: first Loop 29 renders showed oversized dark canyon side masses and a crushed alpine roof. The final `loop29b` patch reduced canyon wall scale and pushed meshes outward, brightened canvas/roof/cladding albedo, and applied bounded hero-specific SSAO/shadow/exposure shaping.
  - Python compile exited 0.
  - Release build exited 0: `[OK] Build complete in 48.4s`; final no-work rebuild exited 0: `[OK] Build complete in 3.5s`.
  - `git diff --check` exited 0 except expected CRLF warnings.
  - Campsite `aaa_hero_mesh_campsite_loop29b` rendered VALID; quality green (`purple_fraction=0.8012`, `avg_luma=0.3397`, `nonblack_fraction=0.9880`); graphics green (`dark_contact_area_fraction=0.2606`); Director validation green; runtime `tent_shells=1 cabin_cladding=0 roof_layers=0 canyon_meshes=0 pbr_layers=8 silhouette_masks=6`.
  - Desert `aaa_hero_mesh_desert_loop29b` rendered VALID; quality green (`turquoise_fraction=0.3874`, `avg_luma=0.2738`, `nonblack_fraction=0.9673`); graphics green (`dark_contact_area_fraction=0.3238`); Director validation green; runtime `tent_shells=1 cabin_cladding=0 roof_layers=0 canyon_meshes=12 pbr_layers=8 silhouette_masks=6`.
  - Alpine `aaa_hero_mesh_alpine_loop29b` rendered VALID; quality green (`avg_luma=0.1125`, `cool_fraction=0.8832`, `nonblack_fraction=0.8424`); graphics green (`dark_contact_area_fraction=0.8330`); Director validation green; runtime `tent_shells=0 cabin_cladding=20 roof_layers=10 canyon_meshes=0 pbr_layers=8 silhouette_masks=6`.
  - Kitchen smoke `regression_kitchen_aaa_loop29` rendered VALID and quality green (`avg_luma=0.4300`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` still reports forbidden fridge assets, missing ridge, focal visibility, and purple-water failures. Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` still fails and now includes `missing_hero_mesh_material_overhaul`.
  - Visual inspection: Loop 29 is a narrow hero-overbuild/material checkpoint, not a solved AAA pass. It improves some hero readability, but campsite/desert/alpine still read flat, cluttered, stylized, and kit-built. The next loop should attack renderer/material lighting response and shadow shaping directly rather than adding more overlay geometry.
- Image metrics alone cannot certify AAA quality; this loop uses them only to catch obvious flat/blockout failures and relies on runtime evidence for deterministic features.
- Generated validation renders now intentionally use SSAO/SSR/shadows instead of forced DXR. Re-enable DXR only behind a separate density/BLAS budget gate.
- Objective graphics gates are green, but visual inspection still shows asset/shot-fidelity limits: low-poly tree silhouettes, generic canyon composition, and remaining stage-like flatness. This is the next asset-fidelity front, not a blocker for this graphics-pass checkpoint.
- A stronger gate must distinguish "valid scene" from "good still": first-class world geometry, shot-depth bands, material-zone diversity, and runtime logs are required before subjective review is meaningful.
- The v3 campsite ridge artifact is a graphics-fidelity known-bad, not a semantic/color quality known-bad. The original `gen_a_foggy_mountain_campsite_beside_0` artifact remains the correct quality oracle for the user-reported fridge/gray-water failure.
- One global closer camera profile improves campsite/desert stills but breaks cabin prompts by cropping the cabin into a wall; generated exteriors need adaptive shot profiles.
- Native procedural/shader passes can improve staging and evidence, but the remaining gap is now visibly asset/geometry fidelity: low-poly silhouettes, simple cabin/camp meshes, and coarse mountain backdrops.
- Loop 9 red proof:
  - `python tools\scene_graphics_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\aaa_graphics_campsite_loop9_0_ir.json --png build\bin\logs\aaa_graphics_campsite_loop9_0.png --log build\bin\logs\aaa_graphics_campsite_loop9.out` exited 1 with `missing_asset_fidelity_detail`.
  - The same new failure was observed for `aaa_graphics_desert_loop9` and `aaa_graphics_alpine_loop10`; old green artifacts lacked `asset_fidelity` IR and runtime hero/backdrop detail logs.
- Loop 9 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.asset_fidelity`, uses detailed pine variants, and adds conservative catalog dressing for campsite/canoe prompts without invalid solver overlaps.
  - `src\Core\Engine_Scenes.cpp` now parses asset fidelity metadata and renders procedural campsite seams/guy-lines/stakes/embers/tripod/foreground twigs, cabin siding/trim/mullions/porch/steps/chimney/warm spill, and extra backdrop silhouette layers.
  - `tools\scene_graphics_gate.py` now requires `asset_fidelity` evidence and runtime logs `created hero asset detail` plus `created backdrop silhouette detail`.
- Loop 9 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0: `[OK] Build complete in 14.9s` after the final refinement.
  - Campsite `aaa_graphics_campsite_loop13` rendered VALID with 58 objects; quality gate exited 0 with purple ROI `purple_fraction=0.7642`; graphics gate exited 0 with residual warning `weak_image_contact_metric`; runtime log shows `created hero asset detail cabin_facade=0 camp=27 foreground=6` and `created backdrop silhouette detail layers=4`.
  - Desert canyon `aaa_graphics_desert_loop13` rendered VALID with 48 objects; quality gate exited 0 with turquoise ROI `turquoise_fraction=0.4219`; graphics gate exited 0 with residual warning `weak_image_contact_metric`; runtime log shows canyon walls/strata plus `camp=27` and `backdrop silhouette detail layers=5`.
  - Alpine cabin `aaa_graphics_alpine_loop13` rendered VALID with 50 objects; quality gate exited 0 (`cool_fraction=0.872`, `nonblack_fraction=1.0`); graphics gate exited 0 with no warnings; runtime log shows `created hero asset detail cabin_facade=32 camp=0 foreground=0` and `backdrop silhouette detail layers=4`.
  - Director IR validation exited 0 for campsite/desert/alpine Loop 13 packets.
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports the fridge/missing-ridge/purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and now also reports `missing_asset_fidelity_detail`.
  - Kitchen smoke `regression_kitchen_aaa_loop13` rendered VALID and quality gate exited 0 (`avg_luma=0.4585`, `nonblack_fraction=1.0`).
- Visual inspection after Loop 9:
  - Improvement is real: visible tent ropes/stakes/embers/tripod, cabin siding/trim/porch/chimney, and denser ridge silhouettes are present in captures.
  - Remaining `HUMAN-GATE` gap is still large: canyon walls are planar/low-poly, trees and camp props are visibly stylized, some overlay geometry reads too flat, and moonlight/storm prompts still need stronger sky/time-of-day coherence. Next front should target geometry/material realism and atmospheric time-of-day, not another metadata-only gate.
- Loop 10 red proof:
  - `aaa_graphics_alpine_loop13` failed the strengthened graphics gate with `missing_atmospheric_time_of_day`.
  - `aaa_graphics_desert_loop13` failed the strengthened graphics gate with `planar_cliff_geometry`.
- Loop 10 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.atmosphere_fidelity` and `graphics_pass.geometry_realism`.
  - `tools\scene_graphics_gate.py` requires atmosphere evidence for moonlight/storm prompts and cliff erosion evidence for canyon prompts.
  - `src\Core\Engine_Scenes.cpp` now darkens moonlight/storm sky presentation, fixes exposure lower for authored night atmosphere, increases storm/fog density, renders haze bands and rain streaks, and adds canyon-wall erosion ridges/vertical cracks.
- Loop 10 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0: `[OK] Build complete in 31.1s`.
  - Alpine `aaa_graphics_alpine_loop14` rendered VALID; quality gate exited 0 (`avg_luma=0.1352`, `cool_fraction=0.8598`, `nonblack_fraction=0.9998`); graphics gate exited 0; runtime log shows `atmospheric pass night_sky=on storm_layers=4 haze_layers=4 rain_streaks=28`.
  - Desert canyon `aaa_graphics_desert_loop14` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4233`); graphics gate exited 0 with residual warning `weak_image_contact_metric`; runtime log shows `created cliff erosion detail ridges=18 cracks=14`.
  - Campsite `aaa_graphics_campsite_loop14` rendered VALID; quality and graphics gates exited 0; runtime log shows fog/haze atmosphere path with `night_sky=off`.
  - Director IR validation exited 0 for campsite/desert/alpine Loop 14 packets.
  - Known-bad quality and graphics oracles with `--expect-fail` exited 0.
  - Kitchen smoke `regression_kitchen_aaa_loop14` rendered VALID and quality gate exited 0 (`avg_luma=0.4634`, `nonblack_fraction=1.0`).
- Visual inspection after Loop 10:
  - Alpine now reads closer to night/storm with a dark sky, cabin glow, visible rain, and retained inspectability.
  - Desert cliff details are visible as erosion/crack lines, but the wall mass is still fundamentally planar and stylized. The next front should attack mesh silhouette/asset realism or integrate higher-fidelity cliff/tree/prop assets rather than adding more line overlays.
- Loop 11 heartbeat proof:
  - `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label codex_aaa_push_proof --timeout 1 --poll 1` exited by timeout after 1s.
- Loop 11 red proof:
  - `python tools\scene_graphics_gate.py --prompt "a sunny desert canyon campsite with red rocks and a turquoise river" --ir build\bin\logs\aaa_graphics_desert_loop14_0_ir.json --png build\bin\logs\aaa_graphics_desert_loop14_0.png --log build\bin\logs\aaa_graphics_desert_loop14.out` exited 1 with only `missing_catalog_cliff_assets`.
- Loop 11 implementation:
  - `tools\scene_graphics_gate.py` now counts catalog `cliff_*` assets and requires at least four for canyon prompts.
  - `tools\scene_compiler.py` now scatters six conservative canyon flank cliff assets: `cliff_large_rock`, `cliff_cornerLarge_rock`, and `cliff_blockSlope_rock`.
- Loop 11 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Desert canyon `aaa_graphics_desert_loop17` rendered VALID with 54 objects; quality gate exited 0 (`turquoise_fraction=0.4212`, `nonblack_fraction=1.0`); graphics gate exited 0 with residual warning `weak_image_contact_metric`; Director IR validation exited 0.
  - Fresh IR contains six catalog cliffs: `cliff_blockSlope_rock 2`, `cliff_cornerLarge_rock 2`, and `cliff_large_rock 2`.
  - Campsite `aaa_graphics_campsite_loop17` rendered VALID with 58 objects; quality gate exited 0 (`purple_fraction=0.7429`, `nonblack_fraction=1.0`); graphics gate exited 0 with residual warning `weak_image_contact_metric`; Director IR validation exited 0.
  - Alpine `aaa_graphics_alpine_loop17` rendered VALID with 50 objects; quality gate exited 0 (`avg_luma=0.1352`, `cool_fraction=0.8599`, `nonblack_fraction=0.9998`); graphics gate exited 0; Director IR validation exited 0.
  - Kitchen smoke `regression_kitchen_aaa_loop17` rendered VALID and quality gate exited 0 (`avg_luma=0.4618`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports the fridge/missing-ridge/purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and still reports the expected graphics-fidelity failures.
- Visual inspection after Loop 11:
  - Catalog cliffs add real mesh silhouettes to canyon IRs, but they do not close the AAA gap by themselves. The next slice should target surface/material realism and repeated low-poly asset reads, not another metadata-only gate.
- Loop 12 red proof:
  - Strengthened `tools\scene_graphics_gate.py` to require `surface_material_richness` IR plus runtime logs for material breakup decals and vegetation/scrub clusters.
  - `aaa_graphics_campsite_loop17`, `aaa_graphics_desert_loop17`, and `aaa_graphics_alpine_loop17` failed the strengthened gate with `missing_surface_material_richness`.
- Loop 12 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.surface_material_richness` with bounded counts for ground decals, rock/lichen or desert-strata patches, vegetation/scrub clusters, and hero material lines.
  - `src\Core\Engine_Scenes.cpp` now parses that contract and renders material breakup decals, canyon-wall desert strata marks, grass/dry-scrub surface clusters, and cabin/tent material line geometry.
  - `tools\scene_gen.py` now writes each render's engine harness output to `build\bin\logs\<out_name>.out`, fixing the stale-log verifier issue where graphics gates could fall back to `cortex_last_run.txt`.
- Loop 12 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0: `[OK] Build complete in 48.3s`.
  - Campsite `aaa_graphics_campsite_loop20` rendered VALID with 58 objects; quality gate exited 0 (`purple_fraction=0.742`, `nonblack_fraction=1.0`); graphics gate exited 0 with residual warning `weak_image_contact_metric`; Director IR validation exited 0. Runtime log shows `created material breakup decals ground=20 rock_lichen=14 desert_strata=0 hero_lines=24` and `created vegetation surface clusters clusters=18 blades=54`.
  - Desert canyon `aaa_graphics_desert_loop20` rendered VALID with 54 objects; quality gate exited 0 (`turquoise_fraction=0.4209`, `nonblack_fraction=1.0`); graphics gate exited 0 with residual warning `weak_image_contact_metric`; Director IR validation exited 0. Runtime log shows `created canyon wall layer(s) 6`, `created material breakup decals ground=18 rock_lichen=2 desert_strata=16 hero_lines=24`, and `created vegetation surface clusters clusters=10 blades=30`.
  - Alpine cabin `aaa_graphics_alpine_loop20` rendered VALID with 50 objects; quality gate exited 0 (`avg_luma=0.1351`, `cool_fraction=0.8608`, `nonblack_fraction=0.9998`); graphics gate exited 0; Director IR validation exited 0. Runtime log shows material breakup decals and vegetation surface clusters.
  - Kitchen smoke `regression_kitchen_aaa_loop20` rendered VALID and quality gate exited 0 (`avg_luma=0.4721`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports the fridge/missing-ridge/purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and now also reports `missing_surface_material_richness`.
- Visual inspection after Loop 12:
  - Surface detail is visible: ground breakup, tent/cabin line detail, vegetation/scrub clusters, and canyon-wall stains now show in the stills. The remaining gap is still source geometry/style: smooth low-poly cliffs, simplified props, and primitive tent/cabin forms. The next front should attack mesh silhouette and prop realism directly.
- Loop 13 heartbeat proof:
  - `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa_graphics_loop13_proof --timeout 1 --poll 1` exited by timeout after 1s.
- Loop 13 red proof:
  - Current Loop 20 campsite/desert/alpine artifacts failed the strengthened graphics gate with `missing_mesh_silhouette_realism`.
- Loop 13 implementation:
  - `tools\scene_graphics_gate.py` now requires `graphics_pass.mesh_silhouette_realism` plus runtime evidence for faceted cliff meshes and hero silhouette bevel/detail.
  - `tools\scene_compiler.py` now emits bounded mesh silhouette counts: six canyon vertical bands, twelve canyon overhangs, eighteen hero bevel details, and nine prop depth layers for hero prompts.
  - `src\Core\Engine_Scenes.cpp` now builds multi-band faceted canyon wall meshes with normals, adds canyon overhang blocks, and renders tent/cabin hero silhouette bevel/eave/hem/depth geometry.
- Loop 13 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0: `[OK] Build complete in 9.4s`.
  - Campsite `aaa_graphics_campsite_loop21` rendered VALID; quality gate exited 0 (`purple_fraction=0.741`, `nonblack_fraction=1.0`); graphics gate exited 0 using `build\bin\logs\aaa_graphics_campsite_loop21_0.out` with residual warning `weak_image_contact_metric`; Director IR validation exited 0. Runtime log shows `created hero silhouette bevel detail cabin=0 camp=18 prop_depth_layers=9`.
  - Desert canyon `aaa_graphics_desert_loop21` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4215`, `nonblack_fraction=1.0`); graphics gate exited 0 using `build\bin\logs\aaa_graphics_desert_loop21_0.out` with residual warning `weak_image_contact_metric`; Director IR validation exited 0. Runtime log shows `created faceted cliff mesh vertical_bands=6 overhangs=12` and `created hero silhouette bevel detail cabin=0 camp=18 prop_depth_layers=9`.
  - Alpine cabin `aaa_graphics_alpine_loop21` rendered VALID; quality gate exited 0 (`avg_luma=0.1344`, `cool_fraction=0.8593`, `nonblack_fraction=0.9998`); graphics gate exited 0 using `build\bin\logs\aaa_graphics_alpine_loop21_0.out`; Director IR validation exited 0. Runtime log shows `created hero silhouette bevel detail cabin=18 camp=0 prop_depth_layers=9`.
  - Kitchen smoke `regression_kitchen_aaa_loop21` rendered VALID and quality gate exited 0 (`avg_luma=0.4466`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports `forbidden_asset_class`, `missing_prompt_entity`, `focal_visibility_fail`, and `purple_water_roi_fail`.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and still reports the expected graphics-fidelity failures, including `missing_mesh_silhouette_realism`.
- Visual inspection after Loop 13:
  - Improvement is visible: canyon walls have faceted bands and ledges, tent hems/flap depth/gear depth show in the campsite still, and cabin eaves/porch/foundation silhouette detail show in the alpine still.
  - Remaining `HUMAN-GATE` gap is still dominated by source/catalog stylization: repeated low-poly trees, simplified camp/cabin meshes, sparse close vegetation, and limited true shadowing/occlusion complexity.
- Loop 13 learning:
  - Fresh `scene_gen` outputs store runtime logs as `build\bin\logs\<out_name>_0.out` for iteration 0. Passing `build\bin\logs\<out_name>.out` to the graphics gate correctly fails because runtime evidence is absent.
- Loop 14 heartbeat proof:
  - `node Z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa_graphics_loop14_proof --timeout 1 --poll 1` exited by timeout after 1s.
- Loop 14 red proof:
  - `aaa_graphics_campsite_loop21`, `aaa_graphics_desert_loop21`, and `aaa_graphics_alpine_loop21` failed the strengthened graphics gate with only `missing_naturalistic_ecology_assets`.
- Loop 14 implementation:
  - `tools\scene_graphics_gate.py` now requires `graphics_pass.naturalistic_ecology` plus runtime evidence for scanned ecology assets.
  - `tools\scene_compiler.py` now emits biome-aware naturalistic ecology counts. Non-desert prompts get scanned grass, bush, fern, trunk, branch, stump, and moss/rock detail; desert/canyon prompts get dry branches, trunks/stumps, and rocks without grass.
  - `src\Core\Engine_Scenes.cpp` now loads/uploads the existing naturalistic showcase meshes and instances them in bounded foreground/flank clusters with texture hooks already used by asset-led scenes.
- Loop 14 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0: `[OK] Build complete in 26.4s`.
  - Campsite `aaa_graphics_campsite_loop23` rendered VALID sequentially with no retry; quality gate exited 0 (`purple_fraction=0.7436`, `nonblack_fraction=1.0`); graphics gate exited 0 using `build\bin\logs\aaa_graphics_campsite_loop23_0.out`; Director IR validation exited 0. Runtime log shows `created naturalistic ecology assets grass=14 bush=6 fern=5 trunks=3 branches=3 stumps=2 moss_rocks=5`.
  - Desert canyon `aaa_graphics_desert_loop23` rendered VALID sequentially with no retry; quality gate exited 0 (`turquoise_fraction=0.4192`, `nonblack_fraction=1.0`); graphics gate exited 0 using `build\bin\logs\aaa_graphics_desert_loop23_0.out`; Director IR validation exited 0. Runtime log shows `created naturalistic ecology assets grass=0 bush=0 fern=0 trunks=2 branches=6 stumps=2 moss_rocks=5`.
  - Alpine cabin `aaa_graphics_alpine_loop23` rendered VALID sequentially with no retry; quality gate exited 0 (`avg_luma=0.1344`, `cool_fraction=0.8593`, `nonblack_fraction=0.9998`); graphics gate exited 0 using `build\bin\logs\aaa_graphics_alpine_loop23_0.out`; Director IR validation exited 0. Runtime log shows `created naturalistic ecology assets grass=14 bush=6 fern=5 trunks=3 branches=3 stumps=2 moss_rocks=5`.
  - Kitchen smoke `regression_kitchen_aaa_loop23` rendered VALID and quality gate exited 0 (`avg_luma=0.2697`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports the fridge/missing-ridge/purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and still reports the expected graphics-fidelity failures, including `missing_naturalistic_ecology_assets`.
- Visual inspection after Loop 14:
  - Scanned ecology is visible as additional brush/debris/rock breakup, especially on campsite/alpine flanks, and desert no longer gets distracting grass artifacts after the refinement.
  - Remaining `HUMAN-GATE` gap: the stills are still stylized and contact shadows remain visibly weak; the next objective front should promote weak contact-shadow image metrics from warnings into a real occlusion loop.
- Loop 14 learning:
  - Running multiple `scene_gen` captures in parallel created timeout retries; rerunning the same prompts sequentially completed cleanly in one pass. Future capture verification should be sequential unless intentionally testing contention.
- Loop 15 red proof:
  - Strengthened `tools\scene_graphics_gate.py` to require `image_contact_occlusion` runtime evidence and hard dark contact-shadow image evidence.
  - `aaa_graphics_campsite_loop23` failed with `missing_image_contact_occlusion_pass` and `weak_contact_shadow_image_metric`.
  - `aaa_graphics_desert_loop23` failed with `missing_image_contact_occlusion_pass` and `weak_contact_shadow_image_metric`.
  - `aaa_graphics_alpine_loop23` already had enough dark contact image pixels, but still failed `missing_image_contact_occlusion_pass`.
- Loop 15 implementation:
  - `tools\scene_compiler.py` now emits `graphics_pass.image_contact_occlusion` with bounded deep receiver patch counts and explicit dark-contact edge/area targets.
  - `src\Core\Engine_Scenes.cpp` now parses the contract and renders small dark receiver slivers from contact patches plus extra camp hero anchors.
  - `tools\scene_graphics_gate.py` now reports both `dark_contact_fraction` and `dark_contact_area_fraction`. The original edge-only metric was audited as too brittle, so the gate requires runtime evidence plus the contact-area target while keeping edge density telemetry.
- Loop 15 verifier evidence:
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0: `[OK] Build complete in 3.5s` after final code changes.
  - Campsite `aaa_graphics_campsite_loop28` rendered VALID sequentially; quality gate exited 0 (`purple_fraction=0.7424`, `nonblack_fraction=1.0`); graphics gate exited 0 with `dark_contact_area_fraction=0.0048`; Director IR validation exited 0. Runtime log shows `created image contact occluders patches=44 target_dark_contact=0.0020`.
  - Desert canyon `aaa_graphics_desert_loop28` rendered VALID sequentially; quality gate exited 0 (`turquoise_fraction=0.4188`, `nonblack_fraction=1.0`); graphics gate exited 0 with `dark_contact_area_fraction=0.0067`; Director IR validation exited 0. Runtime log shows `created image contact occluders patches=44 target_dark_contact=0.0020`.
  - Alpine cabin `aaa_graphics_alpine_loop28` rendered VALID sequentially; quality gate exited 0 (`avg_luma=0.1344`, `cool_fraction=0.8593`, `nonblack_fraction=0.9998`); graphics gate exited 0 with `dark_contact_fraction=0.0122`; Director IR validation exited 0. Runtime log shows `created image contact occluders patches=16 target_dark_contact=0.0020`.
  - Kitchen smoke quality gate on `regression_kitchen_aaa_loop23` exited 0 (`avg_luma=0.2697`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports the fridge/missing-ridge/purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and still reports the expected graphics-fidelity failures, including `missing_image_contact_occlusion_pass`.
- Visual inspection after Loop 15:
  - Contact grounding is more legible under camp/desert props, and objective dark-contact area is no longer zero.
  - Remaining `HUMAN-GATE` gap: receiver slivers are still a stylized approximation. A future front should replace them with softer renderer-level contact shadows or a real density-budgeted RT/AO path.
- Loop 16 red proof:
  - Strengthened `tools\scene_graphics_gate.py` to require `graphics_pass.water_shore_integration` and `graphics_pass.soft_occlusion` plus runtime evidence.
  - `aaa_graphics_campsite_loop28`, `aaa_graphics_desert_loop28`, and `aaa_graphics_alpine_loop28` failed with `missing_water_shore_integration_pass` and `missing_soft_occlusion_pass`.
- Loop 16 implementation:
  - `tools\scene_compiler.py` now emits water/shore integration metadata for foam lace, shoreline ripples, wetline bands, reflection glints, and submerged-edge rocks.
  - `tools\scene_compiler.py` now emits a soft-occlusion contract with penumbra patch count, gradient layers, hero anchors, and stronger SSAO/soft-shadow settings.
  - `src\Core\Engine_Scenes.cpp` now parses those contracts and renders bounded shoreline/ripple/wetline/glint/submerged-edge geometry plus terrain-toned soft contact penumbra and localized raised hard contact cores.
  - `tools\scene_graphics_gate.py` now fails water prompts without water/shore integration evidence and all generated exteriors without soft-occlusion evidence.
- Loop 16 verifier evidence:
  - Heartbeat proof and liveness: `aaa-proof` fired by timeout; `aaa-push-loop16` was armed, timed out once during long verification, then was rearmed.
  - `python -m py_compile tools\scene_compiler.py tools\scene_graphics_gate.py tools\scene_quality_gate.py tools\scene_gen.py` exited 0.
  - Release build exited 0 after final C++ changes: `[OK] Build complete in 30.0s`.
  - Campsite `aaa_graphics_campsite_loop16` rendered VALID; quality gate exited 0 (`purple_fraction=0.7458`, `nonblack_fraction=1.0`); graphics gate exited 0 with `dark_contact_area_fraction=0.0041`; Director IR validation exited 0. Runtime log shows `created image contact occluders patches=56`, `created soft contact occlusion penumbra=20 gradient_layers=3 hero_anchors=12`, and `created water shore integration foam_lace=14 ripples=16 wetline_bands=4 reflection_glints=10 submerged_edge_rocks=6`.
  - Desert canyon `aaa_graphics_desert_loop16` rendered VALID; quality gate exited 0 (`turquoise_fraction=0.4199`, `nonblack_fraction=1.0`); graphics gate exited 0 with `dark_contact_area_fraction=0.0066`; Director IR validation exited 0. Runtime log shows the same water/soft/contact systems with 56 contact occluders.
  - Alpine cabin `aaa_graphics_alpine_loop16` rendered VALID; quality gate exited 0 (`avg_luma=0.1348`, `cool_fraction=0.8593`, `nonblack_fraction=0.9998`); graphics gate exited 0 with `dark_contact_fraction=0.0129`; Director IR validation exited 0. Runtime log shows `created image contact occluders patches=18`, `created soft contact occlusion penumbra=14 gradient_layers=2 hero_anchors=8`, and water shore integration.
  - Kitchen smoke `regression_kitchen_aaa_loop16` rendered VALID and quality gate exited 0 (`avg_luma=0.4635`, `nonblack_fraction=1.0`).
  - Known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` with `--expect-fail` exited 0 and still reports the fridge/missing-ridge/purple-water failures.
  - Known-bad graphics oracle `v3_campsite_ridge_test_0` with `--expect-fail` exited 0 and now also reports `missing_water_shore_integration_pass` and `missing_soft_occlusion_pass`.
- Visual inspection after Loop 16:
  - Improvement is visible in shore/water staging and contact grounding, and the first black-puddle version was corrected to terrain-toned soft penumbra.
  - Remaining `HUMAN-GATE` gap: outputs are still stylized and overlay-driven; low-poly cliffs, simple tent/cabin/camp assets, and coarse background geometry are now the dominant ceiling again.
- Loop 16 learning:
  - In this render path, alpha-blended penumbra overlays can read much darker than intended. Soft occlusion needs terrain-toned albedo plus separate small hard contact cores; relying on alpha alone produces black-puddle artifacts.
- Loop 17 red proof:
  - Strengthened `tools\scene_graphics_gate.py` to require `graphics_pass.hero_environment_geometry` plus runtime evidence.
  - Current Loop 16 campsite/desert/alpine artifacts failed with `missing_hero_environment_geometry`.
- Loop 17 implementation:
  - `tools\scene_compiler.py` now emits high-detail camp/cabin kit, mountain/cliff massing, shoreline prop, irregular tree silhouette, and support prop counts.
  - `src\Core\Engine_Scenes.cpp` now parses the contract and renders thick runtime geometry for camp gear/tent pieces, cabin log/rafter/porch/foundation/woodpile pieces, mountain/cliff massing, shoreline driftwood/stones, and irregular tree silhouettes.
  - `tools\scene_graphics_gate.py` now fails generated exteriors without this IR/runtime evidence.
- Loop 17 verifier evidence:
  - Python compile exited 0.
  - Final Release rebuild exited 0: `[OK] Build complete in 66.0s`.
  - Campsite `aaa_graphics_campsite_loop17b` rendered VALID; quality green (`purple_fraction=0.8847`); graphics green (`dark_contact_area_fraction=0.0187`); Director IR validation green; runtime `camp=34 mountain_layers=5 shoreline_props=10 tree_silhouettes=12`.
  - Desert canyon `aaa_graphics_desert_loop17b` rendered VALID; quality green (`turquoise_fraction=0.4216`); graphics green (`dark_contact_area_fraction=0.0069`); Director IR validation green; runtime `camp=34 mountain_layers=5 cliff_mass=14 shoreline_props=10`.
  - Alpine cabin `aaa_graphics_alpine_loop17` rendered VALID; quality green (`avg_luma=0.1499`, `cool_fraction=0.8591`); graphics green (`dark_contact_fraction=0.0048`); Director IR validation green; runtime `cabin=30 mountain_layers=5 shoreline_props=10 tree_silhouettes=12`.
  - Kitchen smoke `regression_kitchen_aaa_loop17` rendered VALID and quality green (`avg_luma=0.4728`).
  - Known-bad quality oracle still fails the fridge/missing-ridge/focal-visibility/purple-water fixture. Known-bad graphics oracle with explicit empty log still fails and now includes `missing_hero_environment_geometry`.
- Loop 17 learning:
  - First `aaa_graphics_campsite_loop17` failed `purple_water_roi_fail` because non-canyon mountain massing became near-side walls in the fixed water ROI. First `aaa_graphics_desert_loop17` failed `turquoise_water_roi_fail` because canyon massing narrowed the sampled river band. Both were fixed by moving Loop 17 massing outward/back; keep semantic color gates strict.
  - Remaining `HUMAN-GATE` gap: outputs are richer but still stylized, and the next serious front should bind Director setpieces/regions to runtime source geometry or attack asset/material ingestion rather than add unstructured clutter.

## BLOCKED / Decisions

None.
