# Loops: AAA Scene Generation

## Grand Goal Contract

The scene generator is done when all checks below pass:

- `tools/scene_quality_gate.py` rejects the known-bad campsite render:
  - prompt: `a foggy mountain campsite beside a purple lake at dawn`
  - IR: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0_ir.json`
  - PNG: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0.png`
- Director IR v3 exists and validates the campsite prompt as a layered scene, not an object list.
- The v3 compiler emits a renderable current-engine IR while preserving v3 graph/prototype/instance quality evidence.
- The default `scene_gen.py` path can use v3 for exterior scene generation.
- The known-bad prompt produces a new render that passes objective quality gates.
- At least two novel prompts produce renders and pass objective quality gates:
  - `a stormy alpine lake with a small cabin and blue moonlight`
  - `a sunny desert canyon campsite with red rocks and a turquoise river`
- Existing basic regressions still pass: targeted Python checks plus render smoke on one existing battery prompt.
- HUMAN-GATE: final images feel AA/AAA enough to the user. Autonomous loops can prove only objective gates.

## Baseline

- Start branch: `cleanup/debt-artifacts`, ahead of origin.
- Existing untracked planning files: `CAMPAIGN.md`, `docs/AAA_SCENE_GEN_FANOUT.md`, `docs/AAA_SCENE_GEN_PLAN.md`.
- Latest commits before implementation: `00b48f4`, `e401361`, `d31c28f`.
- Heartbeat proof: `node z:\328\CMPUT328-A2\codexworks\301\heartbeat\bin\hb.mjs wait --label aaa-scene-gen-proof --timeout 1 --poll 1` exited with `[hb TIMEOUT]` after 1s on 2026-07-03.

## Verifier Registry

| Verifier | Trust | What It Proves | Proof |
|---|---|---|---|
| `scene_quality_gate.py` known-bad fixture | trusted | Current bad render is rejected for the right reasons | Known-bad expect-fail exited 0 with `forbidden_asset_class`, `missing_prompt_entity`, `purple_water_roi_fail`, `focal_visibility_fail`; controlled good fixture exited 0 |
| Director IR schema self-test | trusted | v3 packet has required layers/prototypes/quality contract | `director_ir_v3.py --example campsite` and `--validate` exited 0 |
| v3 compiler smoke | trusted | v3 compiles to current v2 IR without forbidden roles and with prompt-critical evidence | Loop 3 campsite IR-only gate exited 0; novel alpine/desert v3 validate exited 0 |
| render smoke | trusted | Engine renders emitted IR to PNG without blank/corrupt frames | Campsite, alpine, desert, and kitchen render smoke all produced PNGs and passed frame visibility checks |
| novel prompt gate | trusted | Generalization beyond one prompt | Alpine cabin moonlight and desert canyon turquoise river renders passed objective quality gates |

## Loop Contracts

### Loop 1: Known-Bad Quality Gate

Invariant: the current failed campsite output is rejected by a trusted objective gate with actionable failure reasons.

Entry:

- known-bad IR and PNG exist under `build/bin/logs`.
- verifier does not exist yet.

Scope:

- in: `tools/scene_quality_gate.py`, optional artifact folder under `build/bin/logs/scene_quality`.
- out: generator behavior, renderer behavior, engine C++.

Verifier:

- `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\gen_a_foggy_mountain_campsite_beside_0_ir.json --png build\bin\logs\gen_a_foggy_mountain_campsite_beside_0.png --expect-fail`
- Red proof: command must fail without the expected bad-scene assertions or pass only when it records the expected failures.
- Green proof: command exits 0 in `--expect-fail` mode and reports `forbidden_asset_class`, `missing_prompt_entity`, `purple_water_roi_fail`, and `focal_visibility_fail`.

Exit:

- verifier green in expect-fail mode with expected reason set.

Escape:

- stop if image/IR missing or PIL unavailable and no reasonable stdlib fallback.

Status: done

### Loop 2: Director IR v3 Schema

Invariant: the campsite prompt can be represented as layered Director IR v3 with graph/prototype/instance/quality evidence.

Scope:

- in: `tools/director_ir_v3.py`, examples under `build/bin/logs/director_v3`.
- out: renderer and current v2 pipeline.

Verifier:

- `python tools\director_ir_v3.py --example campsite --out build\bin\logs\director_v3\campsite_v3.json`
- `python tools\director_ir_v3.py --validate build\bin\logs\director_v3\campsite_v3.json`

Exit:

- generated example validates and includes required `scene_layers`, `spatial_regions`, `generator_graph`, `asset_prototypes`, `instance_groups`, `lighting_look`, `quality`.

Status: done

### Loop 3: v3 Compiler Skeleton

Invariant: v3 compiles to current v2 engine IR while preserving evidence that the scene has ridge/lake/campsite/dawn/fog intent.

Scope:

- in: `tools/scene_compiler.py`, `tools/scene_director.py`, small integration in `tools/scene_gen.py`.
- out: C++ renderer features.

Verifier:

- `python tools\scene_compiler.py --in build\bin\logs\director_v3\campsite_v3.json --out build\bin\logs\director_v3\campsite_v2_ir.json`
- `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\director_v3\campsite_v2_ir.json --no-png --require-ir-only`

Exit:

- compiled IR has no forbidden exterior roles and has prompt-critical metadata for campsite, lake, ridge, dawn, fog.

Status: done

### Loop 4: v3 Exterior Path In `scene_gen.py`

Invariant: exterior prompts use v3 path and still render through current engine IR.

Scope:

- in: `tools/scene_gen.py` and v3 Python modules.
- out: C++ renderer unless compiler proves impossible.

Verifier:

- `python tools\scene_gen.py "a foggy mountain campsite beside a purple lake at dawn" --no-critic --name v3_campsite_test --fast`
- quality gate on produced PNG/IR.

Exit:

- output exists and gate passes objective checks except any explicitly marked C++-blocked visual ridge check.

Status: done

### Loop 5: Renderer World-Layer Gap

Invariant: if v2 cannot visually render ridge/shore world layers, add the smallest C++ support needed and prove it with a render.

Scope:

- in: `src/Core/Engine_Scenes.cpp`, possibly `tools/scene_compiler.py`.
- out: unrelated renderer systems.

Verifier:

- C++ build.
- render prompt with ridge metadata.
- quality gate sees ridge/world-layer evidence and PNG exists.

Status: done

### Loop 6: Novel Prompt Synthesis

Invariant: two novel prompts pass objective quality gates without per-prompt hand edits.

Verifier:

- `python tools\scene_gen.py "a stormy alpine lake with a small cabin and blue moonlight" --no-critic --name novel_alpine_moon --fast`
- `python tools\scene_gen.py "a sunny desert canyon campsite with red rocks and a turquoise river" --no-critic --name novel_desert_river --fast`
- quality gate on both outputs.

Status: done

## Progress Log

2026-07-03:

- Created loop ledger from siege/fan-out plan.
- Heartbeat proof recorded from `aaa-scene-gen-proof`.
- Armed heartbeat `aaa-scene-gen` with 900s timeout and done-file condition `build/bin/logs/scene_quality/AAA_DONE`; PID 27848.
- Loop 1 green:
  - `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\gen_a_foggy_mountain_campsite_beside_0_ir.json --png build\bin\logs\gen_a_foggy_mountain_campsite_beside_0.png --expect-fail` exited 0.
  - Report path: `build/bin/logs/scene_quality/a_foggy_mountain_campsite_beside_a_purple_lake_a/quality_gate_report.json`.
  - Controlled good fixture: `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\scene_quality\control_good\good_ir.json --png build\bin\logs\scene_quality\control_good\good.png --out build\bin\logs\scene_quality\control_good` exited 0.
- Loop 2 green:
  - `python tools\director_ir_v3.py --example campsite --out build\bin\logs\director_v3\campsite_v3.json` exited 0.
  - `python tools\director_ir_v3.py --validate build\bin\logs\director_v3\campsite_v3.json` exited 0 with `scene_type: mountain_lake_campsite`.
- Loop 3 green:
  - `python tools\scene_compiler.py --in build\bin\logs\director_v3\campsite_v3.json --out build\bin\logs\director_v3\campsite_v2_ir.json` exited 0 with `objects=57 lights=1`.
  - `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\director_v3\campsite_v2_ir.json --no-png --require-ir-only` exited 0 with only `visual_checks_skipped`.
- Loop 4 green:
  - First render exposed real failures: old compiler scatter overlapped objects and water still measured gray (`purple_water_roi_fail`, `avg_saturation=0.1014`).
  - Fixed compiler collision placement, fixed `--no-critic` so it does not invoke the vision loop, forwarded v3 water material controls to C++, and added a water-shader authored-color path for explicit water colors.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release` exited 0.
  - `python tools\scene_gen.py "a foggy mountain campsite beside a purple lake at dawn" --no-critic --name v3_campsite_test --fast` exited 0 with `backend=director_v3`, `objects=54`, `VALID`.
  - `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\v3_campsite_test_0_ir.json --png build\bin\logs\v3_campsite_test_0.png` exited 0 with `avg_rgb=[0.6365,0.5408,0.6152]`, `avg_saturation=0.2042`, `purple_fraction=0.5578`.
- Loop 5 opened:
  - Visual inspection of `build\bin\logs\v3_campsite_test_0.png` shows the lake and campsite are present, but the mountain/ridge layer is still metadata-only. Next action: add native procedural ridge/backdrop geometry from `environment.background.ridge_layers`.
- Loop 5 green:
  - Added native procedural ridge/backdrop meshes from `environment.background.ridge_layers`.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release` exited 0.
  - `python tools\scene_gen.py "a foggy mountain campsite beside a purple lake at dawn" --no-critic --name v3_campsite_ridge_test --fast` exited 0 with `backend=director_v3`.
  - Runtime log recorded `generative_exterior: created 2 procedural ridge layer(s)`.
  - `python tools\scene_quality_gate.py --prompt "a foggy mountain campsite beside a purple lake at dawn" --ir build\bin\logs\v3_campsite_ridge_test_0_ir.json --png build\bin\logs\v3_campsite_ridge_test_0.png` exited 0 with purple ROI `purple_fraction=0.8846`.
- Loop 6 opened:
  - Rejected the inherited shortcut where a cabin prompt compiled to `tent_detailedClosed`.
  - Added `environment.structures` lowering for cabins and native procedural cabin geometry with wood body, gabled roof, opaque emissive windows, and warm window light.
  - Added objective gates for cabin structures, canyon/red-rock intent, storm rough-water/fog controls, whole-frame nonblank rendering, turquoise/blue water, and rendered moonlight coolness.
  - Negative controls:
    - black alpine artifact failed with `render_blank_or_underlit`.
    - first readable alpine rerender failed with `moonlight_render_coolness_fail`.
    - original known-bad campsite still failed with `forbidden_asset_class`, `missing_prompt_entity`, `purple_water_roi_fail`, and `focal_visibility_fail`.
  - Fixed a D3D12 fence timeout caused by putting procedural cabin windows into the transparent `glass` preset; changed them to opaque emissive panels. Alpine runtime stopped timing out and logs show clean shutdown.
  - Added moonlight/storm compiler controls: `look.time=moonlight`, `cool_overcast` sky, stronger storm fog, cool terrain/water/scatter palette, and renderer-side cool grade/IBL/background exposure handling.
  - `powershell -NoProfile -ExecutionPolicy Bypass -File .\rebuild.ps1 -Config Release` exited 0.
  - `python tools\scene_gen.py "a stormy alpine lake with a small cabin and blue moonlight" --no-critic --name novel_alpine_moon --fast` exited 0 with `backend=director_v3`, `objects=50`, `VALID`; runtime log recorded `created 1 procedural cabin structure(s)` and `created 2 procedural ridge layer(s)`.
  - Alpine quality gate exited 0 with frame metrics `avg_luma=0.3064`, `cool_fraction=0.4842`, `nonblack_fraction=0.9999`.
  - `python tools\scene_gen.py "a sunny desert canyon campsite with red rocks and a turquoise river" --no-critic --name novel_desert_river --fast` exited 0 with `backend=director_v3`, `objects=54`, `VALID`.
  - Desert quality gate exited 0 with turquoise ROI `avg_rgb=[0.4035,0.5072,0.5527]`, `turquoise_fraction=0.381`, `avg_saturation=0.2759`.
  - Regression bundle green:
    - `python -m py_compile tools\scene_gen.py tools\director_ir_v3.py tools\scene_compiler.py tools\scene_quality_gate.py` exited 0.
    - `python tools\director_ir_v3.py --validate build\bin\logs\novel_alpine_moon_director_v3.json` exited 0.
    - `python tools\director_ir_v3.py --validate build\bin\logs\novel_desert_river_director_v3.json` exited 0.
    - `python tools\scene_gen.py "a cozy kitchen with a wooden table and plants" --no-critic --name regression_kitchen --fast` exited 0 with `backend=codex`, `setting=interior`, `VALID`.
    - Kitchen frame gate exited 0 with `avg_luma=0.4573`, `nonblack_fraction=1.0`.

## Learnings

- The current validity gate is not trusted for quality; it passed a render with `kitchenfridge` in a mountain scene.
- The first trusted verifier must be a known-bad gate, not a render quality assertion.
- Verifier lesson: even quality gates can repeat the production bug. First version of ridge detection accepted `kitchenfridge` because it used substring matching; fixed by role-filtering ridge evidence.
- Verifier lesson: IR-only semantic gates missed a black PNG caused by a runtime fence timeout. Whole-frame luminance/coverage is now required for any rendered PNG.
- Runtime lesson: small procedural emissive panes should stay on the opaque path; using the transparent `glass` preset in the cabin structure triggered a command-queue fence timeout in the alpine scene.

## BLOCKED / Decisions

None. HUMAN-GATE remains: the user still decides whether the current images meet the AA/AAA visual bar.
