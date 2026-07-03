# Campaign: AAA Graphics Pass For Generated Exteriors

## Win Condition

Generated exterior scenes must move beyond semantic blockouts into inspectable high-fidelity stills: shaped terrain, grounded props, material variation, stronger contact occlusion/shadows, water/shore integration, and runtime evidence that the high-quality renderer path is active.

2026-07-03 update: the `9c38cea` checkpoint is now treated as a baseline graphics pass, not completion. The next autonomous target is stronger world/shot/material fidelity: canyon walls and strata for canyon prompts, foreground framing geometry, material-zone variation, manipulated lighting evidence, shader-backed material terms, occlusion/surface layers, and adaptive hero-scale cameras that avoid both tiny blockouts and cropped cabin walls.

Autonomous gates can prove hard failures are gone. The final "AAA enough" call remains `HUMAN-GATE`.

## Constraints & Anti-goals

- Protect Director IR v3 semantics from commit `03fdb1e`; do not regress campsite, alpine cabin, desert canyon, or legacy kitchen smoke.
- Do not fake quality by weakening existing `scene_quality_gate.py`.
- New graphics checks must reject the known bad frame `build/bin/logs/v3_campsite_ridge_test_0.png`.
- Prefer native engine features already present: SSAO, SSR, shadows, material normal/procedural/wetness controls, water optics, and procedural meshes.
- Keep render safety: no unbounded GPU stress loops; use frequent heartbeat waits around long build/render runs.

## Terrain Map

Known true:

- The v3 exterior path renders coherent prompt entities, purple/turquoise water, procedural ridges, and cabins.
- The current campsite render is still a flat strip with disconnected props, weak grounding, low-poly ridges, and minimal contact/material staging.
- Renderer controls exist for SSAO/SSR/shadows/bloom/fog/water/material detail.
- The generated exterior runtime still creates planar land/seabed meshes in `src/Core/Engine_Scenes.cpp`.

Known false:

- Better semantic resolution alone will not reach the requested graphics bar.
- A color gate for purple water is not a fidelity gate.

Unknown:

- How far a procedural terrain/contact/material pass can push quality without importing a larger asset kit.
- Whether image heuristics can robustly judge "AAA"; treat them as partial and pair them with IR/runtime evidence.

## Solved Ground

| What | Evidence | Date |
|---|---|---|
| Director IR v3 exterior default is green | `LOOPS.md`, commit `03fdb1e` | 2026-07-03 |
| Known-bad visual critique fixture exists | `build/bin/logs/v3_campsite_ridge_test_0.png` | 2026-07-03 |
| Heartbeat fires | `hb wait --label aaa-graphics-proof --timeout 1 --poll 1` timed out as expected | 2026-07-03 |

## Approach Tree

| # | Approach class | Prediction | Cheapest probe | Kill criteria | Status |
|---|---|---|---|---|---|
| 1 | Graphics gate | A hard-failure gate can reject flat/blockout scenes before subjective review. | Add `scene_graphics_gate.py`; known-bad fixture must fail. | Dead if it cannot reject old PNG/IR without brittle prompt-specific thresholds. | won |
| 2 | Procedural terrain + grounding | Native heightfield terrain and contact decals will remove the planar scene feel. | Replace flat land strip with terrain mesh and add authored ground contact disks. | Dead if new render still has flat-plane metrics/logs or destabilizes prompts. | won |
| 3 | Material/look pass | IR/runtime material profiles plus stronger AO/shadow settings will create visible surface variation and depth. | Compile material metadata and apply normal/procedural/wetness/specular controls at runtime. | Dead if logs/IR show controls but pixels do not change after render A/B. | won |
| 4 | Asset fidelity | Remaining gap may be mostly source asset quality. | After graphics pass, compare output and list missing hero assets. | live residual / HUMAN-GATE |
| 5 | World/shot fidelity gate | Current green desert/campsite stills fail if the verifier requires foreground occluders, depth bands, material zones, and canyon wall/strata evidence. | Strengthen `scene_graphics_gate.py` and run it on `aaa_graphics_desert_0_ir.json`. | Dead if it only catches prompt-specific fixture names instead of structural evidence. | won |
| 6 | Procedural world geometry slice | Native procedural side walls, talus, strata strips, and foreground rocks can reduce the flat stage read without imported assets. | Add IR contract + runtime geometry logs; render novel campsite/alpine/desert prompts. | Dead if build/render is unstable or pixels/logs show no visible change. | won |
| 7 | Shader material + occlusion slice | Existing material constants and overlay geometry can make the pass materially richer without unsafe DXR defaults. | Require advanced shader terms, occlusion ribbons, terrain creases, pebbles, shore foam/wet glints, and runtime logs. | Dead if pixels/logs show no visible change or if quality gates regress. | won |
| 8 | Adaptive shot camera slice | One closer camera improves campsites but can crop cabins; scene-type camera profiles should improve both. | Require shot-camera runtime evidence and render campsite/desert/alpine. | Dead if camera changes break water/color/visibility gates or crop a prompt class. | won |
| 9 | Asset/procedural fidelity slice | The remaining ceiling is visible hero/backdrop construction detail: cabin facade parts, campsite ropes/stakes/embers, and layered ridge silhouettes. | Require `asset_fidelity` IR + runtime logs, prove old green artifacts fail, then add native procedural detail. | Dead if dense procedural detail destabilizes captures or remains invisible after render/log A/B. | won |
| 10 | Atmosphere + cliff realism slice | Storm/moonlight/canyon prompts need authored atmosphere and non-planar cliff detail beyond color grading and wall counts. | Require atmospheric and cliff-realism IR/runtime evidence, prove Loop 13 artifacts fail, then add native controls/detail geometry. | Dead if overlays regress prompt semantics or image gates; reduce counts before changing gates. | won |
| 11 | Catalog cliff asset slice | Existing Kenney nature cliff assets can add real silhouette mass to canyon prompts faster than more line overlays. | Require canyon IR to include several `cliff_*` catalog assets, prove Loop 14 fails, then scatter cliff assets on canyon flanks. | Dead if solver validity regresses or assets look worse than procedural fallback. | won |
| 12 | Surface material richness slice | The current plateau is toy-like flat surfaces; native decal/cluster geometry can add readable dirt, lichen, wood grain, fabric wrinkle, and vegetation/scrub breakup without forcing DXR. | Require `surface_material_richness` IR plus runtime material-breakup and vegetation-cluster logs, prove Loop 17 artifacts fail, then render novel prompts. | Dead if overlays make scenes noisy, break water/color/visibility gates, or fail to show in pixels/logs after build. | won |
| 13 | Mesh silhouette realism slice | The latest stills now fail on primitive silhouettes: flat cliff sheets, boxy cabin volumes, and simple tent props. Faceted cliff-wall meshes plus bounded hero bevel/eave/hem geometry should improve form without changing the asset catalog. | Require `mesh_silhouette_realism` IR plus runtime faceted-cliff and hero-silhouette logs, prove Loop 20 artifacts fail, then render campsite/desert/alpine. | Dead if extra mesh complexity destabilizes captures, crops heroes, or remains visually indistinct after build. | won |
| 14 | Naturalistic ecology asset slice | The latest stills still read sparse and game-kit-like because close vegetation and debris are low-poly/repeated. Existing scanned naturalistic assets can add non-repeated grass, fern/bush, branch, stump, trunk, and moss-rock detail without external downloads. | Require `naturalistic_ecology` IR plus runtime scanned-asset logs, prove Loop 21 artifacts fail, then render campsite/desert/alpine. | Dead if scanned assets fail to load/upload, make desert prompts forest-like, or destabilize captures. | won |
| 15 | Deep contact occlusion slice | Campsite/desert stills remain visually floaty because the image contact-shadow metric is effectively zero even with runtime AO metadata. Small dark receiver patches under prompt props should make grounding visible without forcing DXR. | Promote weak dark-contact image evidence to a graphics failure, prove Loop 23 campsite/desert fail, then add a bounded runtime `image_contact_occlusion` pass. | Dead if dark patches read as obvious black stains or break water/visibility gates; reduce patch size/count before weakening the image threshold. | won |
| 16 | Water/shore + soft occlusion slice | Loop 28 stills have hard receiver slivers but weak shoreline integration and no broad soft contact pass. Authored foam/ripples/wetline/glints plus terrain-toned penumbra geometry should improve water grounding and shadow depth without enabling unsafe DXR. | Require `water_shore_integration` and `soft_occlusion` IR/runtime evidence, prove Loop 28 artifacts fail, then render campsite/desert/alpine. | Dead if soft occlusion becomes black puddles, breaks contact metrics, or water-color/visibility gates regress. | won |

## Fronts

| Front | Mechanism | State | Last advance |
|---|---|---|---|
| Gate | loops | done | Known-bad v3 campsite fixture rejected by `scene_graphics_gate.py --expect-fail` |
| Runtime terrain/contact/material | loops | done | Campsite/alpine/desert rendered with heightfield/contact/material logs and graphics gates green |
| Regression synthesis | loops | done | Release build, Python compile, known-bad gates, novel prompts, and kitchen smoke green |
| World/shot/material fidelity | loops | done checkpoint | World geometry, shader materials, occlusion/surface layers, and adaptive cameras verified on campsite/desert/alpine |
| Asset/procedural fidelity | loops | done checkpoint | Loop 9 verified campsite/desert/alpine with close-range hero construction and richer backdrop silhouettes |
| Atmosphere/cliff realism | loops | done checkpoint | Loop 10 verified authored storm/moonlight atmosphere and canyon erosion detail on novel prompts |
| Catalog cliff assets | loops | done checkpoint | Loop 11 verified six catalog cliff meshes in fresh canyon IR with campsite/alpine/kitchen regressions green |
| Surface material richness | loops | done checkpoint | Loop 12 verified runtime material-breakup decals, vegetation/scrub clusters, and per-render logs on campsite/desert/alpine |
| Mesh silhouette realism | loops | done checkpoint | Loop 13 verified faceted canyon mesh/overhangs and hero bevel/eave/hem details on campsite/desert/alpine |
| Naturalistic ecology assets | loops | done checkpoint | Loop 14 verified scanned ecology assets on campsite/desert/alpine; desert uses dry branches/stumps/rocks, not grass |
| Deep contact occlusion | loops | done checkpoint | Loop 15 verified runtime contact occluders and dark contact-area image evidence on campsite/desert/alpine |
| Water/shore + soft occlusion | loops | done checkpoint | Loop 16 verified shoreline foam/ripples/wetline/glints, terrain-toned soft penumbra, and preserved hard contact-area evidence on campsite/desert/alpine |
| Asset fidelity | self/HUMAN-GATE | residual | Catalog quality is now the dominant visible limit: low-poly silhouettes, simple cabin/camp meshes, coarse mountain backdrops |

## Beat Log

2026-07-03:

- Re-oriented from `main` at `03fdb1e`.
- Read prior `CAMPAIGN.md`/`LOOPS.md`; v3 semantic slice is protected solved ground.
- Proved heartbeat once with `aaa-graphics-proof`.
- Current highest-leverage action: build a graphics-fidelity negative gate, then implement the terrain/contact/material vertical slice against it.
- Took over the in-flight AAA graphics pass after a black campsite render.
- Attributed the black frame to validation captures forcing DXR/BLAS on dense generated exteriors. Same IR rendered cleanly in 3.7s with `CORTEX_DISABLE_RT=1`; the terrain/material path itself was not the crash cause.
- Added a generated-capture DXR guard in `tools/render_ir.ps1`: SSAO/SSR/shadows stay on by default, DXR is opt-in via `CORTEX_ENABLE_GENERATIVE_DXR=1` until it has a density budget.
- Added heightfield terrain metadata, contact/shore grounding, material profiles, runtime renderer AO/SSR/shadow controls, per-object material lowering, and normal/specular overrides.
- Release build green in 98.8s.
- Verified campsite, alpine cabin moonlight, and desert turquoise river with semantic and graphics gates. All objective gates are green; final visual fidelity remains `HUMAN-GATE`.
- Reopened after user rejected the baseline as still too basic/disconnected.
- Proved the stronger gate red on `build/bin/logs/aaa_graphics_desert_0_ir.json`: old desert canyon now fails `insufficient_material_zone_variation`, `missing_world_depth_geometry`, `desert_canyon_blockout`, and `tree_heavy_desert_staging`.
- Continued after user rejected stopping early. Proved new gate failures on current green artifacts, then implemented shader-backed material metadata, runtime material constants, occlusion ribbons, terrain creases, pebbles, shore foam/wet glints, and runtime evidence logs.
- Added adaptive generated-exterior camera profiles. Campsite/desert use `closer_midground_hero`; cabin/alpine uses `balanced_cabin_hero` after visual inspection showed the closer camera cropped the cabin into a wall.
- Final checkpoint evidence: Python compile green; Release build green (`[OK] Build complete in 17.2s`); campsite loop9, desert loop9, and alpine loop10 render valid and pass quality + strengthened graphics gates; Director IR validation green for all three; kitchen smoke green; known-bad quality oracle `gen_a_foggy_mountain_campsite_beside_0` still fails for the right semantic/color reasons; known-bad graphics oracle `v3_campsite_ridge_test_0` still fails the graphics gate.
- Reopened again after user called out underscoping. Local-loop check: prior Loop 8 green proved renderer/material/camera controls but did not attack the visible asset ceiling. New highest-leverage front is Loop 9, a procedural asset fidelity slice with a red-first gate for hero construction detail and backdrop silhouette density.
- Loop 9 checkpoint: old loop9 artifacts failed the strengthened graphics gate with `missing_asset_fidelity_detail`; new `aaa_graphics_campsite_loop13`, `aaa_graphics_desert_loop13`, and `aaa_graphics_alpine_loop13` all render valid and pass quality + graphics gates. Release build, Python compile, Director IR validation, known-bad oracles, and kitchen smoke are green. Visual inspection confirms added hero/backdrop detail, but the overall AAA target remains open: geometry is still stylized/planar and atmospheric time-of-day is not strong enough.
- Opened Loop 10 after visual inspection: the cabin facade reads better, but alpine moonlight still looks like daytime cloud sky with a cool grade, and desert canyon walls are visibly planar. Next red-first gate targets atmosphere and cliff geometry realism.
- Loop 10 checkpoint: `aaa_graphics_alpine_loop14` passes with darker authored night/storm atmosphere and rain/haze runtime evidence; `aaa_graphics_desert_loop14` passes with cliff erosion detail runtime evidence; campsite regression, Director IR validation, known-bad oracles, Release build, Python compile, and kitchen smoke are green. Visual residual: cliff/wall mass and many props are still stylized and planar, so the next front needs richer mesh silhouettes or higher-fidelity assets.
- Loop 11 checkpoint: proved `aaa_graphics_desert_loop14` fails only `missing_catalog_cliff_assets`, then generated `aaa_graphics_desert_loop17` with six `cliff_*` catalog assets. Desert, campsite, alpine, Director IR validation, known-bad oracles, Python compile, and kitchen smoke are green. Residual: catalog cliffs add silhouette mass but still read as stylized low-poly assets, so the next front must improve material/geometry realism rather than declare the AAA push complete.
- Loop 12 checkpoint: proved current Loop 17 artifacts fail `missing_surface_material_richness`, added compiler/runtime material breakup decals, vegetation/scrub clusters, tent/cabin material lines, and per-artifact `scene_gen` log capture. Release build, Python compile, campsite/desert/alpine quality + graphics + Director IR validation, kitchen smoke, and known-bad oracles are green. Residual: the pass adds visible surface detail, but the underlying low-poly catalog/primitive geometry is still the next visible ceiling.
- Loop 13 checkpoint: proved Loop 20 artifacts fail `missing_mesh_silhouette_realism`, added `mesh_silhouette_realism` IR, faceted cliff-wall vertical bands, canyon overhang blocks, and campsite/cabin hero silhouette bevel/eave/hem/depth geometry. Release build green (`[OK] Build complete in 9.4s`); Python compile green; Loop 21 campsite/desert/alpine quality, graphics, and Director IR validation green using the per-render `_0.out` logs; kitchen smoke green; both known-bad oracles still fail for the right reasons. Residual: form is less flat, but the source/catalog style is still visibly low-poly and repeated.
- Loop 14 checkpoint: proved Loop 21 artifacts fail only `missing_naturalistic_ecology_assets`, then added compiler/runtime support for scanned naturalistic grass, bush, fern, trunk, branch, stump, and rock assets. Release build green (`[OK] Build complete in 26.4s`); Python compile green; Loop 23 campsite/desert/alpine rendered sequentially without retries and passed quality, graphics, and Director IR validation; kitchen smoke green; known-bad quality and graphics oracles still fail. Residual: scanned ecology adds visible breakup, but contact-shadow image metrics remain weak for campsite/desert.
- Loop 15 checkpoint: promoted weak contact-shadow evidence into a graphics failure, proved Loop 23 artifacts fail, audited the original edge-only metric as too brittle, added a dark contact-area metric plus runtime `image_contact_occlusion` receiver patches. Loop 28 campsite/desert/alpine quality, graphics, and Director IR validation are green; campsite dark contact area is `0.0048`, desert `0.0067`, both above the `0.004` target; Release build, Python compile, kitchen smoke, and known-bad oracles are green. Residual: this is still a stylized receiver-shadow approximation, not real soft RT contact shadows.
- Reopened after the user called out the remaining material/shader/occlusion gap. Proved Loop 28 campsite/desert/alpine fail the new Loop 16 graphics gate with `missing_water_shore_integration_pass` and `missing_soft_occlusion_pass`.
- Loop 16 checkpoint: added `graphics_pass.water_shore_integration` and `graphics_pass.soft_occlusion`, runtime shoreline foam/ripple/wetline/reflection/submerged-edge geometry, terrain-toned soft contact penumbra, and a larger localized hard-contact core budget. Release build green (`[OK] Build complete in 30.0s`); Python compile green; Loop 16 campsite/desert/alpine quality + graphics gates green; Director IR validation green; kitchen smoke green; known-bad quality and graphics oracles still fail for the right reasons. Final metrics: campsite `dark_contact_area_fraction=0.0041`, desert `0.0066`, alpine `dark_contact_fraction=0.0129`; runtime logs show water shore integration and soft contact occlusion for all three prompts. Residual: water/shore and occlusion read better, but the scene remains stylized with low-poly kit assets and visible overlay construction.

## Learnings

- The existing objective gate is necessary but insufficient; it accepts images that are semantically correct but visually blockout-level.
- Dense generated exteriors must not blindly inherit the validation path's forced DXR. The first-frame BLAS workload can stall before capture even when the same scene is stable through SSAO/SSR/shadows.
- The new graphics gate proves missing hard features, not "AAA." It must remain paired with human image review and future asset-fidelity work.
- Contact grounding needs careful restraint: too many bright overlay disks make the scene look more artificial even when metrics pass.
- A valid generated scene is not necessarily a good still. The reopened gate now requires structural world geometry, shot-depth bands, material-zone diversity, and canyon-specific wall/strata evidence before subjective review.
- The `v3_campsite_ridge_test` artifact is no longer a semantic/color quality oracle; it is a graphics-fidelity oracle. The original user-bad `gen_a_foggy_mountain_campsite_beside_0` artifact remains the quality oracle for fridge/missing-ridge/non-purple-water failures.
- Procedural shader/occlusion/camera passes improve the stills, but they do not solve the asset-fidelity ceiling. The next serious front is better hero/environment assets or richer procedural meshes/textures, not another semantic gate.
- Loop 9 improved visible asset construction, but the next plateau is now geometry/material realism: canyon/cliff meshes need more believable form, close props need less cube/cylinder obviousness, and moonlight/storm skies need to read as authored atmosphere instead of daytime sky with a cool grade.
- Loop 10 improved atmosphere and cliff breakup, but line overlays cannot substitute for real mesh silhouette complexity. The next serious slice should either use existing high-detail cliff/tree assets from the catalog/pretrained asset folders or add more volumetric/procedural mesh generation for rock faces and vegetation.
- Loop 11 showed catalog cliff assets are stable in the solver and visible in IR/runtime outputs, but the visual ceiling is now material/mesh fidelity: low-poly cliff blocks, simplified props, and repeated stylized trees need richer surface treatment or procedural complexity.
- Loop 12 improved material breakup and fixed a verifier-gauge issue: `scene_gen` now saves the engine harness log beside each render (`<out_name>.out`), so graphics gates no longer have to rely on stale `cortex_last_run.txt` fallback evidence.
- Loop 13 improved cliff and hero silhouettes, but it also exposed that generated artifacts save runtime logs as `<out_name>_0.out` for iteration 0; graphics gates must use that path or they correctly fail for missing runtime evidence.
- Loop 14 showed parallel `scene_gen` captures create artificial GPU timeout retries; sequential generated captures complete cleanly in 5-8s on the same assets. Future render verification should run captures sequentially unless testing contention deliberately.
- Loop 15 showed the old edge-only dark-contact image metric was a weak gauge: it missed visible receiver shadows and did not separate old/new outputs. The replacement uses runtime evidence plus a dark contact-area fraction, while retaining edge density as supporting telemetry.
- Loop 16 showed alpha-blended soft occlusion can render as opaque-looking puddles in this path; terrain-toned penumbra colors plus small raised hard cores preserve the objective contact metric without large black pools. The next serious front should attack the source geometry/asset ceiling rather than add more overlay layers.

## BLOCKED / Decisions Needed

None.
