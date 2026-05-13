# Asset-Led Showcase Review Notes

## 2026-05-13 Capture Review

Command:

`powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -SmokeFrames 90`

Latest reviewed run:

`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_032316_958_56684_065cd3cb`

Result: capture generation passed, but the new asset-led scenes are **not public-gallery ready**.

Findings:

- `coastal_cliff_foundry_hero`: no longer falls back to the office HDRI, but still reads as a miniature platform pasted over a coastal HDRI. The platform edge is too visible, scale is wrong, and the cliff wall is a flat slab.
- `rain_glass_pavilion_hero`: glass and rain are visible, but the scene is still a floating pavilion tile in the sky. It needs real surrounding ground, stronger garden/background framing, and less repetitive roof/floor striping.
- `desert_relic_gallery_hero`: after environment fix it still needs richer ruin geometry and less blocky plinth/wall staging before it can represent a material showcase.
- `neon_alley_material_market_hero`: the night environment is now correct, but the alley geometry is still too boxy and the scene reads as a small display set, not a believable alley.
- `forest_creek_shrine_hero`: water, rocks, and vegetation exist, but it still reads as a floating diorama. It needs authored banks, tree massing, and a non-flat shrine silhouette.

Decision:

- Do not commit the generated asset-led screenshots to `docs/media` yet.
- Do not add the new images to the release package manifest yet.
- Keep scenes at `seeded_runtime_wip` in `showcase_scenes.json`.
- Next scene-art pass should focus on scale, camera closeness, terrain/backdrop integration, and replacing box silhouettes with more natural grouped meshes.

## 2026-05-13 Focused Asset-Led Review

Command:

`powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/asset_led_review2 -SmokeFrames 90`

Latest reviewed run:

`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_035700_873_80908_8fa3900f`

Result: focused asset-led capture generation passed with `captures=15 size=1920x1080 preset=public_high`, but the scenes remain **not public-gallery ready**.

Changes reviewed:

- Added `-AssetLedOnly` capture filtering so WIP scene-art review does not dirty the established public gallery.
- Tightened hero cameras for all five asset-led scenes.
- Added contact/support geometry: foundry rail footplates and lower rails, pavilion terrace skirts and glass base rails, desert steps/broken blocks, neon sign brackets/curb/display base, and forest creek banks/shrine steps.
- Switched `rain_glass_pavilion` to `night_city` for the intended rainy night context.

Findings:

- `coastal_cliff_foundry_hero`: rail contact is clearer and the worst flat wall is reduced, but the set still reads as rails on a platform in front of an HDRI. It needs a real authored cliff/industrial structure instead of box backdrops.
- `rain_glass_pavilion_hero`: the night environment and glass/reflection read are stronger. Remaining issues are large flat translucent walls, over-bright strip light, and weak exterior/garden grounding.
- `desert_relic_gallery_hero`: closer framing helps the relic read, but the scene is still a blocky tan plinth with a ring. It needs authored ruin meshes, stone breakup, sand piles, and less box architecture.
- `neon_alley_material_market_hero`: this is the strongest of the new set. Sign brackets and wet reflections help, but the market still needs denser storefront assets and signage that does not read as blank panels.
- `forest_creek_shrine_hero`: closer framing hides some edge problems, but the shrine remains box-built and the vegetation/background are still flat walls. It needs organic banks, real shrine silhouette, tree massing, and better water readability.

Decision:

- Do not commit these WIP screenshots to `docs/media`.
- Keep all five new scenes at `seeded_runtime_wip`.
- Next implementation slice should replace the most obvious box silhouettes with authored mesh clusters or stronger procedural mesh primitives before more public-capture work.

## 2026-05-13 Mesh-Variety Asset-Led Review

Command:

`powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/asset_led_review3 -SmokeFrames 90`

Latest reviewed run:

`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_040845_812_74892_4a86a226`

Result: focused asset-led capture generation passed with `captures=15 size=1920x1080 preset=public_high`, but the scenes remain **not public-gallery ready**.

Changes reviewed:

- Added extra coastal boulder anchors to break up the platform edge.
- Added desert cylinders, cones, ceramic vessels, and a bronze pedestal so the scene is no longer only boxes plus a torus.
- Added forest shrine posts, a cone roof, branch/grass assets, and more organic bank detail.

Findings:

- `coastal_cliff_foundry_hero`: extra rocks help foreground breakup, but the composition is still dominated by the HDRI/building background and a flat platform/rail silhouette. This probably needs a different composition, not just more props.
- `desert_relic_gallery_hero`: material and silhouette variety improved slightly, but the camera still reads a clean block plinth against a flat wall. The next pass needs ruin geometry and sand/stone breakup that are visible from the hero camera.
- `forest_creek_shrine_hero`: branch/grass additions help the background, but the foreground shrine still reads as stacked blocks in heavy rain. The next pass needs either a better camera around the organic assets or a substantially stronger shrine mesh.
- `rain_glass_pavilion_hero`: still one of the stronger WIP scenes; needs strip-light/exposure control and less flat perimeter glass.
- `neon_alley_material_market_hero`: still strongest; needs intentional sign graphics, denser storefront breakup, and less billboard-like background dependency.

Decision:

- Do not publish the WIP captures.
- Keep ledger items ALS-006 through ALS-010 as `PARTIAL`.
- Next meaningful jump should be a composition redesign for the weakest hero shots, starting with `coastal_cliff_foundry`, `desert_relic_gallery`, and `forest_creek_shrine`.

## 2026-05-13 Startup-Reapply and Backdrop Review

Command:

`powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/asset_led_review8 -SmokeFrames 90`

Latest reviewed run:

`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260513_121718_108_27588_dbc93a6b`

Result: focused asset-led capture generation passed with `captures=15 size=1920x1080 preset=public_high`, but the scenes remain **not public-gallery ready**.

Changes reviewed:

- Startup now reapplies asset-led scene renderer controls after command-line graphics/environment presets, then reapplies the requested camera bookmark.
- Coastal/desert/forest hero cameras remain widened from the previous pass.
- Coastal gained required cliff wall/crown geometry and more upper basalt massing.
- Desert gained required high ruin wall/lintel geometry.
- Forest canopy blobs were reduced and split into smaller masses, and the foreground mist sheet was removed.

Validation evidence:

- Release target rebuild passed after the startup-reapply and scene-builder changes.
- `run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30` passed with `scenes=5`.
- `run_scene_composition_stability_tests.ps1` passed with `seeds=5`.
- `run_world_shader_contract_tests.ps1` passed with `palettes=5 modes=9`.
- `git diff --check --ignore-submodules=all` passed with line-ending warnings only.

Findings:

- `coastal_cliff_foundry_hero`: renderer controls now reapply correctly, but the hero still depends on the city/coast HDRI and has large flat wall/beam silhouettes. The added cliff wall improves coverage but reads as a slab, so this still needs a true composition redesign or better authored cliff/industrial meshes.
- `desert_relic_gallery_hero`: high ruin geometry adds depth, but the shot still reads as a tan block construction with a city/coast HDRI behind it. The material palette and architecture are not yet strong enough for public screenshots.
- `forest_creek_shrine_hero`: the mist streak issue is fixed, and the canopy is less oversized, but the scene still reads as procedural primitives around a box shrine. It needs a real shrine mesh/silhouette and organic bank/tree art.

Decision:

- Do not publish the WIP captures.
- Keep ALS-006, ALS-008, and ALS-010 as `PARTIAL`.
- Keep ALS-012 and ALS-014 as `PARTIAL`.
- The startup-reapply fix is worth keeping because it prevents public graphics presets from silently overriding asset-led lighting/background contracts.

## 2026-05-13 Forest Creek Detail Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/forest_redesign_review2/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_redesign_review3/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_redesign_review4/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the forest scene remains **not public-gallery ready**.

Changes reviewed:

- Retuned the forest hero camera several times to avoid the wide procedural-looking landscape frame.
- Reduced shrine primitive scale and pushed it out of the focal area.
- Added/required extra creek wall stones and hero fern clusters.
- Lowered scanned boulders/trunks and reduced canopy/backdrop primitive scale to improve contact and hide obvious background construction.

Findings:

- The wide forest composition still exposes floating-looking scanned boulders, hard rectangular creek edges, sky/HDRI dependency, and flat background walls.
- Tight creek-detail framing reduces some background exposure, but it still shows disconnected rocks, visible strip geometry, and a weak shrine silhouette.
- The current asset kit is not enough to sell the forest scene as naturalistic from a wide public camera. This scene needs either better authored terrain/tree/shrine meshes or a redesigned macro-only material showcase.

Decision:

- Do not publish the forest captures.
- Keep `ALS-010` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.
- Treat this as validated WIP evidence that the scene now has stronger contracts and more detailed foreground anchors, not as visual acceptance.

## 2026-05-13 Forest Asset-Kit Scale and Camera Iteration

Commands:

- `cmake -S CortexEngine -B CortexEngine\build`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_kit_policy_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_naturalistic_asset_policy_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_world_shader_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/forest_assetkit_review3/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_assetkit_review4/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_assetkit_review5/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, asset policy remained under budget after removing the oversized rejected root scan, but the forest scene remains **not public-gallery ready**.

Changes reviewed:

- Added committed CC0 Poly Haven assets `tree_stump_01`, `rock_moss_set_01`, and `wild_rooibos_bush` to the naturalistic asset kit.
- Rejected and removed `root_cluster_01` because it exceeded `max_single_asset_bytes` and produced black overhead streaks in runtime review.
- Added runtime texture bindings and forest placements for stump, moss-rock, and bush assets.
- Raised and widened the forest hero camera, narrowed the creek sheet, reduced boulder/trunk/stump/moss-rock scale, and replaced floating primitive canopy blobs with grounded bush placements.

Validation evidence:

- Asset kit policy passed with `assets=11`.
- Naturalistic asset policy passed with `assets=11 bytes=27417004/52428800`.
- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- World shader contract passed with `palettes=5 modes=9`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `forest_assetkit_review5` loaded the new forest asset textures with `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`.

Findings:

- The worst `forest_assetkit_review3` defects were fixed: the giant foreground rock mass and black horizontal log no longer dominate the frame.
- The current hero is more readable as a complete scene, but it still exposes rectangular banks/platforms, a weak box-built shrine, visible strip geometry, sparse twig-like bush silhouettes, and too much procedural sky/background.
- The forest scene needs a stronger authored terrain/shrine solution before it can be accepted as public media; asset additions alone are not enough.

Decision:

- Do not publish the forest captures.
- Keep `ALS-010` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.
- Keep the asset-kit additions except the rejected root scan, because the remaining assets pass policy and improve runtime material coverage.
