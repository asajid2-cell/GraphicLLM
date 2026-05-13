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
