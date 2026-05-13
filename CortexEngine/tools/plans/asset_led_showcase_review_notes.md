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
