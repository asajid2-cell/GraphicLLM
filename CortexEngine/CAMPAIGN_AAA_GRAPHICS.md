# Campaign: AAA Graphics Pass For Generated Exteriors

## Win Condition

Generated exterior scenes must move beyond semantic blockouts into inspectable high-fidelity stills: shaped terrain, grounded props, material variation, stronger contact occlusion/shadows, water/shore integration, and runtime evidence that the high-quality renderer path is active.

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

## Fronts

| Front | Mechanism | State | Last advance |
|---|---|---|---|
| Gate | loops | done | Known-bad v3 campsite fixture rejected by `scene_graphics_gate.py --expect-fail` |
| Runtime terrain/contact/material | loops | done | Campsite/alpine/desert rendered with heightfield/contact/material logs and graphics gates green |
| Regression synthesis | loops | done | Release build, Python compile, known-bad gates, novel prompts, and kitchen smoke green |
| Asset fidelity | self/HUMAN-GATE | residual | Desert/campsite still show catalog/shot-fidelity limits despite objective gates |

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

## Learnings

- The existing objective gate is necessary but insufficient; it accepts images that are semantically correct but visually blockout-level.
- Dense generated exteriors must not blindly inherit the validation path's forced DXR. The first-frame BLAS workload can stall before capture even when the same scene is stable through SSAO/SSR/shadows.
- The new graphics gate proves missing hard features, not "AAA." It must remain paired with human image review and future asset-fidelity work.
- Contact grounding needs careful restraint: too many bright overlay disks make the scene look more artificial even when metrics pass.

## BLOCKED / Decisions Needed

None.
