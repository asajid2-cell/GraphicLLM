# Loops: AAA Graphics Pass

## Grand Goal Contract

All criteria below must be true for the autonomous portion to be complete:

- Known-bad campsite fixture fails the new graphics gate for missing terrain relief, contact/grounding, material pass, and runtime graphics evidence.
- New campsite render passes both `scene_quality_gate.py` and the new graphics gate.
- At least two novel exterior prompts pass the graphics gate without per-prompt code edits.
- Strengthened graphics gate requires advanced shader material terms, surface/occlusion layering, and runtime shot-camera evidence.
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

## Progress Log

2026-07-03:

- Created this separate loop ledger to avoid contaminating the Director IR v3 ledger.
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

- Image metrics alone cannot certify AAA quality; this loop uses them only to catch obvious flat/blockout failures and relies on runtime evidence for deterministic features.
- Generated validation renders now intentionally use SSAO/SSR/shadows instead of forced DXR. Re-enable DXR only behind a separate density/BLAS budget gate.
- Objective graphics gates are green, but visual inspection still shows asset/shot-fidelity limits: low-poly tree silhouettes, generic canyon composition, and remaining stage-like flatness. This is the next asset-fidelity front, not a blocker for this graphics-pass checkpoint.
- A stronger gate must distinguish "valid scene" from "good still": first-class world geometry, shot-depth bands, material-zone diversity, and runtime logs are required before subjective review is meaningful.
- The v3 campsite ridge artifact is a graphics-fidelity known-bad, not a semantic/color quality known-bad. The original `gen_a_foggy_mountain_campsite_beside_0` artifact remains the correct quality oracle for the user-reported fridge/gray-water failure.
- One global closer camera profile improves campsite/desert stills but breaks cabin prompts by cropping the cabin into a wall; generated exteriors need adaptive shot profiles.
- Native procedural/shader passes can improve staging and evidence, but the remaining gap is now visibly asset/geometry fidelity: low-poly silhouettes, simple cabin/camp meshes, and coarse mountain backdrops.

## BLOCKED / Decisions

None.
