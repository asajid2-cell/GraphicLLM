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

## 2026-05-14 Rain Tabletop Vignette Reframe

Commands:

- `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId rain_glass_pavilion`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId rain_glass_pavilion -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/rain_tabletop_vignette_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and capture passed, and the drain macro regression is fixed, but the rain pavilion remains **not public-gallery ready**.

Changes reviewed:

- Synchronized the runtime default and public hero camera around a tighter table/chrome/glass material vignette.
- Tightened the glass-closeup bookmark while keeping the contract-required FOV range.
- Reframed `puddle_chrome` away from the clipped chrome drain macro toward the table, chrome orb, wet surface, glass edge, and floor reflections.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Scene composition stability passed with `seeds=1`.
- Rain asset-led runtime contracts passed with `scene=rain_glass_pavilion`.
- Asset-led public capture `rain_tabletop_vignette_review` passed with `captures=15 size=1920x1080 preset=public_high`.
- Rain hero report recorded `gpu_frame_ms=132.458`, `avg_luma=34.903`, `rt_reflection_signal_avg_luma=0.03780`, `rt_reflection_history_signal_avg_luma=0.03888`, and texture uploads `submitted=13 completed=13 failed=0 pending=0 uploaded=66.67MB`.

Findings:

- The old `puddle_chrome` shot no longer clips into the chrome drain and bright floor channel.
- The hero is now a clearer table/chrome/glass material vignette.
- Rear panels/slats, visible block geometry, and the table-heavy primitive composition still prevent publication.

Decision:

- Do not publish the rain captures.
- Keep `ALS-007` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Neon Storefront Crop Iteration

Commands:

- `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId neon_alley_material_market`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId neon_alley_material_market -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/neon_storefront_crop_review3 -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and capture passed, but the neon market remains **not public-gallery ready**.

Changes reviewed:

- Synchronized the runtime default/hero camera with a moderate right-storefront crop.
- Tightened the materials bookmark around the display case, cyan sign, amber shelf, and right shelf detail.
- Restored the particles bookmark after a tighter experiment exposed a hard right-side studio/background artifact.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Scene composition stability passed with `seeds=1`.
- Neon asset-led runtime contracts passed with `scene=neon_alley_material_market`.
- Asset-led public capture `neon_storefront_crop_review3` passed with `captures=15 size=1920x1080 preset=public_high`.
- Neon hero report recorded `gpu_frame_ms=8.095`, `avg_luma=46.505`, `rt_reflection_signal_avg_luma=0.02678`, `rt_reflection_history_signal_avg_luma=0.02680`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- The empty alley/backdrop exposure is reduced compared with the wider hero.
- The display case, wall panels, and signage still read as primitive blockout forms.
- This crop is a recoverable checkpoint, not release media.

Decision:

- Do not publish the neon captures.
- Keep `ALS-009` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Coastal Crown Grounding Checkpoint

Commands:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/coastal_review14_grounded_crown/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/coastal_review15_rebalanced_rocks/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the coastal cliff foundry remains **not public-gallery ready**.

Changes reviewed:

- Lowered and reduced the upper cliff crown boulders so they no longer read as floating rocks.
- Shortened the diagonal braces and reduced the right industrial silhouette.
- Tested a closer camera, rejected it because foreground rocks swallowed the frame, then reduced the foreground rock mass and settled on a less aggressive hero camera.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `coastal_review15_rebalanced_rocks` rendered 1920x1080 with `renderScale=0.850`, `gpu_frame_ms=3.511`, `avg_luma=93.709`, `rt_reflection_signal_avg_luma=0.08756`, and `rt_reflection_history_signal_avg_luma=0.08772`.
- Texture uploads in `coastal_review15_rebalanced_rocks` completed with `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- The floating-crown issue is reduced.
- The foreground boulder no longer dominates the whole frame.
- The scene is still rejected for public media: the rail/platform assembly is oversized and block-built, the rear walls are flat silhouettes, and the composition still leans on sky/HDRI instead of authored set depth.

Decision:

- Do not publish the coastal captures.
- Keep `ALS-006` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Rain Downward Interior Checkpoint

Commands:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused capture:
  - `CortexEngine/build/bin/logs/rain_review21_downward_interior/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the rain glass pavilion remains **not public-gallery ready**.

Changes reviewed:

- Changed the hero camera to a higher downward interior angle.
- The new framing prioritizes wet floor, table, lantern, glass channels, and reflection response instead of the cloudy horizon.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `rain_review21_downward_interior` rendered 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=4.043`, `avg_luma=41.498`, `rt_reflection_signal_avg_luma=0.07714`, and `rt_reflection_history_signal_avg_luma=0.07711`.
- Texture uploads in `rain_review21_downward_interior` completed with `submitted=14 completed=14 failed=0 pending=0 uploaded=109.33MB`.

Findings:

- The sky/HDRI band is much less dominant than `rain_review20_final_checkpoint`.
- Wet floor and glass/reflection response are easier to judge.
- The scene is still rejected for public media: the table dominates the frame, the lighting is too dark, and rear/blockout geometry is still visible around the pavilion.

Decision:

- Do not publish the rain captures.
- Keep `ALS-007` as `PARTIAL`.
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

## 2026-05-13 Coastal Backdrop and Grounding Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/coastal_review9/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/coastal_review10/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/coastal_review11/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, and the coastal capture is less mismatched, but the scene remains **not public-gallery ready**.

Changes reviewed:

- Moved `coastal_cliff_foundry` from `sunset_courtyard` to `cool_overcast` because `procedural_sky` is a manifest fallback, not a valid `SetEnvironmentPreset` runtime ID.
- Reduced the upper cliff wall/rock mass scale so boulders no longer read as giant floating slabs.
- Lowered and moved the barrel so it sits closer to the deck instead of floating off the right edge.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `coastal_review11` loaded the intended `cool_overcast` environment and rendered cleanly.

Findings:

- The city HDRI mismatch is fixed in the latest coastal capture; the background now reads as sky/clouds rather than unrelated urban waterfront.
- The composition still has visible box-platform construction, rail/post repetition, a large flat cliff block, and weak integration between the foundry channel and shoreline.
- The barrel is improved but still close to the edge of the constructed deck; this scene needs a deeper redesign of the industrial structure and cliff geometry before publication.

Decision:

- Do not publish the coastal captures.
- Keep `ALS-006` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Rain Pavilion IBL and Garden Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/rain_review9/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review10/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review11/visual_validation_rt_showcase.bmp`

Result: build, contracts, and runtime smoke passed, but the rain pavilion remains **not public-gallery ready**.

Changes reviewed:

- Replaced the rain pavilion's `night_city` IBL/default capture environment with `cool_overcast` because the city reflections made the scene read as a transparent box pasted over an unrelated skyline.
- Reduced the glass wall height/thickness and introduced more corner posts, rear mullions, and thinner roof/floor members so the panes read as framed architecture.
- Added rear planter blocks plus scanned `wild_rooibos_bush` and `fern_02` placements to replace the purely flat backdrop.
- Added a lower rear enclosure and vertical garden-screen slats, reduced the warm strip scale/intensity, and moved rain/mist particles away from the foreground.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- World shader contract passed with `palettes=5 modes=9`.
- Asset-led scene contract passed with `scenes=5`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `rain_review11` loaded `cool_overcast`, rendered 1920x1080 with `renderScale=1.000`, and loaded the added fern, bush, and lantern textures with `submitted=11 completed=11 failed=0 pending=0 uploaded=93.33MB`.

Findings:

- The old city-HDRI billboard failure is fixed; the latest frame reads more like a deliberate glass pavilion.
- The scene is still visually WIP: the rear enclosure and slats remain too procedural, plant silhouettes are too dark/sparse, and the foreground floor still carries too much of the composition without enough authored object detail.
- Glass framing, wet floor reflections, and rain particles are materially clearer than before, but this is not yet a public screenshot.

Decision:

- Do not publish the rain pavilion captures.
- Keep `ALS-007` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Forest Creek Narrow-Crop Iteration

Commands:

- `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId forest_creek_shrine`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId forest_creek_shrine -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/forest_creek_narrow_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and capture passed, but the forest creek shrine remains **not public-gallery ready**.

Changes reviewed:

- Tightened the hero camera around the creek/water material read.
- Narrowed the creek sheet and foam-edge strips to reduce the broad flooded-plane read.
- Synchronized the runtime camera with `showcase_scenes.json` and the hand-authored forest seed.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Scene composition stability passed with `seeds=1`.
- Forest asset-led runtime contracts passed with `scene=forest_creek_shrine`.
- Asset-led public capture `forest_creek_narrow_review` passed with `captures=15 size=1920x1080 preset=public_high`.
- Forest hero report recorded `gpu_frame_ms=11.058`, `avg_luma=73.618`, `rt_reflection_signal_avg_luma=0.08920`, `rt_reflection_history_signal_avg_luma=0.08915`, and texture uploads `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`.

Findings:

- The tighter crop makes the water material more prominent than the prior wide hero.
- The scene still exposes flat green platforms, hard water/foam strips, a block-built shrine, floating-looking vegetation/rocks, and sky-wall dependency.
- This is still a procedural diorama, not release media.

Decision:

- Do not publish the forest captures.
- Keep `ALS-010` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Coastal Short-Rail and Lowered-Cliff Iteration

Commands:

- `cmake --build CortexEngine\build --config Release --target CortexEngine`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId coastal_cliff_foundry`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId coastal_cliff_foundry -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/coastal_lowered_cliff_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and high asset-led capture passed, but Coastal remains **not public-gallery ready**.

Changes reviewed:

- Tightened the hero bookmark toward lava, grates, wet basalt, and rock anchors.
- Shortened the lava channel, channel walls, rail runs, lower rails, and furnace crossbeam.
- Lowered and scaled the upper cliff/notch tokens so they sit closer to rear rock masses instead of floating against the sky.

Findings:

- `coastal_cliff_foundry_atmosphere` no longer shows the worst floating sky slabs.
- `coastal_cliff_foundry_hero` still reads as oversized rail/channel construction on a box platform.
- The scene needs a stronger authored foundry/cliff redesign or a much more decisive close-up before publication.

Decision:

- Do not publish the Coastal captures.
- Keep `ALS-006` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Desert Relic Close-Crop Iteration

Commands:

- `cmake --build CortexEngine\build --config Release --target CortexEngine`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId desert_relic_gallery`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId desert_relic_gallery -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/desert_relic_close_crop_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and high asset-led capture passed, but Desert remains **not public-gallery ready**.

Changes reviewed:

- Tightened the hero camera around the bronze ring, glass inlay, mosaic band, and chipped stone.
- Reduced the main plinth, front/rear steps, and front chip proportions to shrink the tan-box face.

Findings:

- `desert_relic_gallery_hero` has a stronger relic/material focal read than the previous wide plinth composition.
- The scene still reads as primitive blockout because oversized columns, rear blocks, reflective spheres, and flat sand/ruin planes dominate the image.
- Publication likely requires real ruin/stone assets or a more radical scene redesign.

Decision:

- Do not publish the Desert captures.
- Keep `ALS-008` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Neon Right-Market Detail Iteration

Commands:

- `cmake --build CortexEngine\build --config Release --target CortexEngine`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId neon_alley_material_market`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId neon_alley_material_market -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/neon_right_market_detail_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and high asset-led capture passed, but the Neon alley remains **not public-gallery ready**.

Changes reviewed:

- Added right storefront inset, service shelf, cyan/amber price tabs, and a low glass display case.
- Added a small right-side cyan light so the new shelf detail is visible in hero/material frames.
- Shifted the `particles` bookmark away from the near right wall so it frames signage, rain, and the display case instead of a black slab.

Findings:

- `neon_alley_material_market_hero` and `materials` now show additional right-side detail and less empty black wall.
- `neon_alley_material_market_particles` is materially better than the previous near-wall composition.
- The scene still reads as a primitive, box-built alley with glowing rectangles and needs stronger authored storefront/facade assets before publication.

Decision:

- Do not publish the Neon captures.
- Keep `ALS-009` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Rain Pavilion Studio Vignette Iteration

Commands:

- `cmake --build CortexEngine\build --config Release --target CortexEngine`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId rain_glass_pavilion`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId rain_glass_pavilion -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/rain_vignette_studio_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and high asset-led capture passed, but the rain pavilion remains **not public-gallery ready**.

Changes reviewed:

- Added contract-covered tabletop chrome/glass accents and a non-emissive warm tray.
- Added `RainPavilion_ChromePuddleRing` so the low puddle/chrome bookmark has a real metallic focal object.
- Tightened the rain public camera bookmarks around the table, glass, wet floor, and chrome detail.
- Moved the rain public/default environment to `studio` to remove the cloudy overcast reflection band.

Findings:

- `rain_glass_pavilion_hero` and `rain_glass_pavilion_glass_closeup` now read more like intentional material vignettes than the previous sky-reflection table shot.
- The rear panels/slats are still visible and remain blockout-like.
- `rain_glass_pavilion_puddle_chrome` now shows chrome, but the framing is too extreme/abstract for publication.

Decision:

- Do not publish the rain pavilion captures.
- Keep `ALS-007` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Desert Relic Focal-Read Checkpoint

Commands:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/desert_review11_relic_focus/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/desert_review12_tile_breakup/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/desert_review13_lintel_grounding/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the desert relic gallery remains **not public-gallery ready**.

Changes reviewed:

- Tightened the hero camera around the relic/plinth so the ring, glass inlay, vessels, tile detail, sand, and scanned stones carry more of the frame.
- Reduced high ruin/lintel massing and lowered broken cap pieces to avoid the worst disconnected geometry.
- Replaced the single solid blue plinth strip with individual mosaic tile blocks.
- Added front stone chips, warmer stone variation, and a scanned `DesertRelic_PlinthStoneAnchor`.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `desert_review13_lintel_grounding` rendered 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=8.181`, `avg_luma=130.359`, `rt_reflection_signal_avg_luma=0.01525`, and `rt_reflection_history_signal_avg_luma=0.01523`.
- Texture uploads in `desert_review13_lintel_grounding` completed with `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- The relic and material accents are more readable than `desert_review10`.
- The blue accent now reads as tiles instead of one huge flat stripe.
- The scene still has public-release blockers: the upper caps/lintels remain primitive, the right wall is a flat slab, the foreground planks still dominate, and the overall architecture still reads as blockout rather than authored ruin geometry.

Decision:

- Do not publish the desert captures.
- Keep `ALS-008` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Forest Shrine Focal-Read Checkpoint

Commands:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/forest_review6_shrine_focus/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_review7_downward/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the forest creek shrine remains **not public-gallery ready**.

Changes reviewed:

- Enlarged the shrine base, capstone, roof, and posts so the focal prop is no longer tiny in the hero camera.
- Increased rear bush massing using existing scanned `wild_rooibos_bush` placements.
- Reframed the hero camera twice; the final checkpoint uses a higher downward view to emphasize creek rocks, water, and shrine scale while reducing the worst horizon exposure from the first close attempt.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `forest_review7_downward` rendered 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=10.195`, `avg_luma=92.704`, `rt_reflection_signal_avg_luma=0.08617`, and `rt_reflection_history_signal_avg_luma=0.08613`.
- Texture uploads in `forest_review7_downward` completed with `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`.

Findings:

- The shrine is more legible than `forest_assetkit_review5`.
- The creek/rock foreground carries more of the image than before.
- The scene is still rejected for public media: the sky/HDRI wall is dominant, the banks are rectangular platforms, the shrine is still block-built, and several branch/plant silhouettes feel scattered rather than naturally rooted.

Decision:

- Do not publish the forest captures.
- Keep `ALS-010` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Forest Low-Creek Material Checkpoint

Commands:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/forest_review8_current_prepass/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_review9_low_creek/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_review10_low_material/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_review11_shrine_attachment/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/forest_review12_creek_material/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the forest creek shrine remains **not public-gallery ready**.

Changes reviewed:

- Reduced broad ground/bank/platform geometry and narrowed creek/foam strips.
- Added scanned boulder cladding around the shrine base and required those cladding tokens in the runtime layout contract.
- Lowered the hero camera through several attempts, ending on a creek/material-biased frame after the wide shrine shot continued to fail.
- Reduced the toy roof/upper stone silhouette and tree-pole heights after the low camera exposed a floating-block read.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `forest_review12_creek_material` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=5.153`, `avg_luma=83.475`, `rt_reflection_signal_avg_luma=0.07028`, and `rt_reflection_history_signal_avg_luma=0.07024`.
- Texture uploads in `forest_review12_creek_material` completed with `submitted=26 completed=26 failed=0 pending=0 uploaded=173.33MB`.

Findings:

- The lower material framing reduces some of the wide rectangular diorama read.
- The scene still fails public review: sky/background exposure is still dominant, the banks remain visibly rectangular, and the shrine remains block-built despite the cladding.
- Further camera tuning is not the right fix; this scene needs authored terrain/shrine meshes or a different macro-material showcase design.

Decision:

- Do not publish the forest captures.
- Keep `ALS-010` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Rain Pavilion Segmented-Screen Checkpoint

Commands:

- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/rain_review13/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review14/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review15/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review16/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review18_studio_builder/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review19_low_close/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/rain_review20_final_checkpoint/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the rain pavilion remains **not public-gallery ready**.

Changes reviewed:

- Replaced the giant rear wood screen slab with two smaller low panels and shorter asymmetric slats.
- Added `RainPavilion_GlassRoofPanel` so the pavilion reads as a constructed glass room instead of only vertical panes.
- Enlarged the scanned `WoodenTable_01`, added `RainPavilion_TableWarmMat`, reduced the lantern scale, and kept the hero at a low interior angle.
- Tested a studio-IBL builder variant and a lower close camera; both were rejected because the studio HDRI mismatch and table silhouette were worse than the overcast checkpoint.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `rain_review20_final_checkpoint` rendered 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=4.118`, `avg_luma=71.066`, `rt_reflection_signal_avg_luma=0.05278`, and `rt_reflection_history_signal_avg_luma=0.05276`.
- Texture uploads in `rain_review20_final_checkpoint` completed with `submitted=14 completed=14 failed=0 pending=0 uploaded=109.33MB`.

Findings:

- The giant flat screen regression from `rain_review13` through `rain_review15` is fixed.
- The hero now has a clearer table/floor/reflection focal read than the interrupted WIP.
- The scene is still too procedural for public media: the cloudy HDRI band is visible through/behind the glass, the rear posts and slats are still blockout geometry, and the right-side panel reads as a flat prop wall.

Decision:

- Do not publish the rain pavilion captures.
- Keep `ALS-007` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Neon Alley Signage and Environment Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/neon_review8/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/neon_review9/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/neon_review10/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/neon_review11/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, and the city-HDRI billboard failure is reduced, but the neon alley remains **not public-gallery ready**.

Changes reviewed:

- Added designed sign-mask bars, a secondary amber blade sign, and display-case interior shelf/post details so the signage no longer reads as only blank emissive rectangles.
- Replaced the giant overhead slab with thinner overhead beams.
- Added awnings, rear service door, pipe stacks, floor breakup patches, and stricter composition tokens for those details.
- Switched the public/default capture path from `night_city` to `studio` with deliberately low IBL/specular intensity because `night_city` reflected as visible city billboards across the walls and display glass.
- Tightened the hero bookmark so the display case, mounted sign, wet floor, and alley wall occupy a more connected frame.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `neon_review11` rendered at 1920x1080 with `renderScale=1.000`, `VB renderedThisFrame=true`, `instances=47`, and texture uploads `submitted=7 completed=7 failed=0 pending=0 uploaded=34.67MB`.

Findings:

- `neon_review8` and `neon_review9` were rejected because the scene looked like floating boxes in front of city-HDRI panels; the blank cyan sign and oversized reflective glass dominated the shot.
- `neon_review10`/`neon_review11` remove the worst city-billboard read and prove the added details are present, but the scene is now too dark and still relies on primitive wall/display geometry.
- The hero is acceptable as a checkpoint, not as a public screenshot. It needs authored storefront meshes, better interior props, more readable magenta/cyan balance, and rain/steam particles that do not obscure the display case.

Decision:

- Do not publish the neon alley captures.
- Keep `ALS-009` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Neon Alley Lit-Market Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_world_shader_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_gpu_particle_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_effects_showcase_smoke.ps1`
- Focused captures:
  - `CortexEngine/build/bin/logs/neon_review12_current_prepass/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/neon_review13_lit_market/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, and the neon shot is more readable, but the neon alley remains **not public-gallery ready**.

Changes reviewed:

- Raised the authored neon exposure to `0.84` and updated the world-palette contract so runtime checks enforce the brighter art direction.
- Widened the hero camera to bring the left storefront back into the frame.
- Reduced the display-case glass/base/shelf footprint so it no longer consumes the entire right foreground.
- Moved the rain effect farther back and added a magenta rear menu board, glyph masks, and left-stall amber/cyan accent strips.
- Strengthened the runtime layout contract to require the new menu/stall-light details.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- World shader contract passed with `palettes=5 modes=9`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- GPU particle contract tests passed.
- Effects Showcase smoke passed.
- Focused capture `neon_review13_lit_market` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=14.053`, `avg_luma=35.128`, and texture uploads `submitted=7 completed=7 failed=0 pending=0 uploaded=34.67MB`.

Findings:

- The new magenta menu board and left stall accent lights make the frame less empty than `neon_review12`.
- The smaller display case is easier to read as glass/chrome instead of a giant transparent block.
- The scene is still too dark for public media, rain streaks/droplets still distract from the case, and the storefront wall/display architecture remains primitive.

Decision:

- Do not publish the neon captures.
- Keep `ALS-009` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Coastal Foundry Rail and Furnace Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/coastal_review12/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/coastal_review13/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the coastal foundry remains **not public-gallery ready**.

Changes reviewed:

- Aligned showcase metadata and visual baseline environment to `cool_overcast`.
- Lowered/tightened the hero bookmark to favor foreground basalt and furnace structure.
- Darkened the metal/furnace materials, added diagonal furnace braces and channel grate slats, reduced rail repetition, and reduced the most dominant upper beam/foreground boulder scale.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `coastal_review13` rendered at 1920x1080 with `renderScale=0.850`, `VB renderedThisFrame=true`, `instances=61`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- `coastal_review12` still had a giant rail/beam across the top of the frame and foreground rocks blocking the scene.
- `coastal_review13` improves that specific defect, but the composition is still visibly primitive: flat rear walls, repeated rail pieces, a floating-looking upper boulder, and rectangular industrial silhouettes dominate.
- This needs a stronger redesign with authored cliff/foundry meshes or a much more decisive close-up that hides the primitive blockout.

Decision:

- Do not publish the coastal captures.
- Keep `ALS-006` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Desert Relic Asset Breakup and Environment Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/desert_review9/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/desert_review10/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, but the desert scene remains **not public-gallery ready**.

Changes reviewed:

- Added scanned `boulder_01` clusters and `dry_branches_medium_01` debris to break up the primitive plinth/ruin silhouette.
- Added darker crevice/shadow strips on the plinth and ground to reduce the single-tan material read.
- Switched `desert_relic_gallery` from `sunset_courtyard` to `cool_overcast` because the sunset environment exposed city/courtyard buildings behind the ruin.
- Reframed the hero camera wider after `desert_review9` showed a bad cropped lintel and city HDRI.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `desert_review10` loaded `cool_overcast`, rendered at 1920x1080 with `renderScale=1.000`, and loaded scanned boulder/branch textures with `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- The city-environment mismatch is fixed in `desert_review10`.
- The scanned rocks/branches help scale and contact, but the scene still reads as blockout architecture: large flat walls, plank-like lintels, and a huge rectangular plinth dominate the frame.
- The material palette is less single-note than before, but it still needs authored ruin geometry, better stone breakup, and more deliberate prop scale before publication.

Decision:

- Do not publish the desert captures.
- Keep `ALS-008` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Desert Relic Blockout-Reduction Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused captures:
  - `CortexEngine/build/bin/logs/desert_review14_wall_breakup/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/desert_review15_plinth_breakup/visual_validation_rt_showcase.bmp`
  - `CortexEngine/build/bin/logs/desert_review16_recessed_blockout/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, and the visible blockout defects are reduced, but the desert scene remains **not public-gallery ready**.

Changes reviewed:

- Widened/raised the hero camera after the closer relic crop made the foreground sand lip and plinth face too dominant.
- Reduced the foreground sand lip, sand drift, and sand ramps so they no longer read as giant tan boards.
- Shortened the right/left ruin returns, rear wall blocks, high lintels, and broken arch caps to reduce floating-bar and flat-wall reads.
- Added front plinth chip overlays to break up the large rectangular face.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `desert_review16_recessed_blockout` rendered at 1920x1080 with `renderScale=1.000`, `gpu_frame_ms=3.644`, `avg_luma=137.017`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- The huge right wall and foreground plank are materially smaller than `desert_review13`.
- The image still reads as a primitive plinth/blockout ruin, with cube/cylinder architecture dominating over naturalistic authored mesh quality.
- The scene needs real stone/ruin mesh assets or a more decisive close-up composition before publication.

Decision:

- Do not publish the desert captures.
- Keep `ALS-008` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-13 Rain Pavilion Interior Focal-Point Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -RuntimeSmoke -SmokeFrames 30`
- Focused capture:
  - `CortexEngine/build/bin/logs/rain_review12/visual_validation_rt_showcase.bmp`

Result: build and contracts passed, and the hero is stronger, but the rain pavilion remains **not public-gallery ready**.

Changes reviewed:

- Added scanned `WoodenTable_01` as `RainPavilion_GroundedInteriorTable`.
- Tightened the hero camera so the frame emphasizes glass panels, the lantern/table interior, warm strip light, and wet floor reflections instead of empty floor and the rear enclosure.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Scene composition stability passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Asset-led runtime scene contracts passed with `scenes=5`.
- Focused capture `rain_review12` loaded table, fern, bush, and lantern textures with `submitted=14 completed=14 failed=0 pending=0 uploaded=109.33MB`.

Findings:

- The table/lantern combination gives the glass pavilion a more believable interior focal point.
- The tighter hero frame is materially better than `rain_review11`, but the rear screens and garden panels still read as procedural dark slabs/slats.
- Do not publish this yet; a later pass should replace the rear architecture/garden with authored mesh work or a more decisive close-up composition.

Decision:

- Do not publish the rain pavilion captures.
- Keep `ALS-007` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Coastal Channel Scale Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId coastal_cliff_foundry`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId coastal_cliff_foundry -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/coastal_channel_scale_review2 -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and the focused high capture passed, but the coastal foundry remains **not public-gallery ready**.

Changes reviewed:

- Raised and widened the hero camera to reduce the near-field glowing channel face and rail dominance.
- Shortened/lowered the lava surface, channel walls, furnace uprights, crossbeam, rail runs, posts, and feet.
- Kept seed metadata and showcase hero bookmark aligned with the runtime camera.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Coastal composition stability passed with `seeds=1`.
- Coastal asset-led runtime contract passed with `scene=coastal_cliff_foundry`.
- Focused asset-led public capture `coastal_channel_scale_review2` passed with `captures=15 size=1920x1080 preset=public_high`.
- Hero report `public_capture_gallery_20260514_015357_916_21488_502f1f12/coastal_cliff_foundry/frame_report_last.json` recorded `gpu_frame_ms=6.253`, `avg_luma=100.531`, `rt_reflection_signal_avg_luma=0.07698`, `rt_reflection_history_signal_avg_luma=0.07730`, and texture uploads `submitted=9 completed=9 failed=0 pending=0 uploaded=120.00MB`.

Findings:

- The foreground rail/channel is no longer the dominant failure in the hero frame.
- The scene still exposes flat backdrop slabs, floating-looking rock supports, and primitive channel/platform construction.
- This is a useful checkpoint, not an acceptance slice; publication still requires stronger authored cliff/structure geometry or a more decisive material close-up.

Decision:

- Do not publish the coastal captures.
- Keep `ALS-006` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Rain Tabletop Detail Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId rain_glass_pavilion`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId rain_glass_pavilion -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/rain_tabletop_detail_review2 -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and the focused high capture passed, but the rain pavilion remains **not public-gallery ready**.

Changes reviewed:

- Lowered and reduced the required rear wood-screen and garden-screen tokens so they compete less with the table.
- Tightened the hero/glass/puddle bookmarks around the tabletop, then pulled the final hero slightly wider after an oversized-prop first capture.
- Added small glass/chrome/ceramic tabletop accents and scaled them down after `rain_tabletop_detail_review` made the chrome props read too large.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Rain composition stability passed with `seeds=1`.
- Rain asset-led runtime contract passed with `scene=rain_glass_pavilion`.
- Focused asset-led public capture `rain_tabletop_detail_review2` passed with `captures=15 size=1920x1080 preset=public_high`.
- Hero report `public_capture_gallery_20260514_021728_170_17612_a1ce27ec/rain_glass_pavilion/frame_report_last.json` recorded `gpu_frame_ms=11.069`, `avg_luma=43.736`, `rt_reflection_signal_avg_luma=0.03131`, `rt_reflection_history_signal_avg_luma=0.03180`, and texture uploads `submitted=13 completed=13 failed=0 pending=0 uploaded=66.67MB`.

Findings:

- The second capture fixes the oversized floating chrome-prop failure from the first tabletop-detail attempt.
- The table still reads as blockout geometry, and the rear panels plus studio reflections remain obvious.
- This remains a checkpoint only; acceptance likely needs a better authored table/interior prop setup or a different rain material composition.

Decision:

- Do not publish the rain captures.
- Keep `ALS-007` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.

## 2026-05-14 Desert Relic Scale Iteration

Commands:

- `cmd /c 'call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build CortexEngine\build --config Release --target CortexEngine'`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_seed_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_showcase_scene_contract_tests.ps1`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_scene_composition_stability_tests.ps1 -SceneId desert_relic_gallery`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_asset_led_scene_contract_tests.ps1 -SceneId desert_relic_gallery -RuntimeSmoke -SmokeFrames 45`
- `powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -AssetLedOnly -OutputDir CortexEngine/build/bin/logs/desert_relic_scale_review -Width 1920 -Height 1080 -SmokeFrames 90`

Result: build, contracts, and the focused high capture passed, but the desert relic scene remains **not public-gallery ready**.

Changes reviewed:

- Tightened the hero camera around the relic after the previous close crop still exposed giant primitive vessels and columns.
- Reduced the bronze ring/pedestal scale and shrank the ceramic sphere-vessels so they no longer dominate the frame.
- Lowered and shortened the high ruin/lintel/arch-column tokens while preserving required layout coverage.

Validation evidence:

- Scene seed contract passed with `seeds=5`.
- Showcase scene contract passed with `scenes=12`.
- Desert composition stability passed with `seeds=1`.
- Desert asset-led runtime contract passed with `scene=desert_relic_gallery`.
- Focused asset-led public capture `desert_relic_scale_review` passed with `captures=15 size=1920x1080 preset=public_high`.
- Hero report `public_capture_gallery_20260514_022535_664_39128_84144666/desert_relic_gallery/frame_report_last.json` recorded `gpu_frame_ms=7.134`, `avg_luma=124.914`, `rt_reflection_signal_avg_luma=0.06056`, `rt_reflection_history_signal_avg_luma=0.06056`, and texture uploads `submitted=8 completed=8 failed=0 pending=0 uploaded=77.33MB`.

Findings:

- The giant reflective vessel failure is materially reduced, and the ring is a cleaner focal object.
- The scene still reads as primitive blockout because the plinth, front rail pieces, rear columns, and backdrop walls are visibly simple shapes.
- Acceptance still needs authored ruin/plinth geometry or a more radical material composition.

Decision:

- Do not publish the desert captures.
- Keep `ALS-008` as `PARTIAL`.
- Keep `ALS-014` as `PARTIAL`.
