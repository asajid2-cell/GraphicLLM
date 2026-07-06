# GenScene Handoff

This file contains zero project state on purpose. State lives in `CURRENT.md`, and only
`tools/run_genscene_acceptance.ps1` may rewrite it after the bootstrap seed.

| File | Role | Writer |
|---|---|---|
| `CURRENT.md` | Accepted state, gate results, tree cleanliness | Acceptance runner only |
| `CURRENT_FAILED.md` | Last failed acceptance attempt | Acceptance runner only |
| `PLAN.md` | Strategy and phase order | Human/agent on phase changes |
| `QUEUE.md` | Active work queue plus accept/reject log | Agent on every outcome |
| `docs/GENSCENE_NEXT_LEVEL_PLAN.md` | Claude review/postmortem evidence | Review artifact |
| This file | Durable rules and entry protocol | Rarely |

If any file contradicts `CURRENT.md`, `CURRENT.md` wins. Fixing the stale file is part of
the current turn.

## Mission

Turn GenScene from a metric-chasing procedural overlay pipeline into a curated prompt-to-scene
system with continuous terrain, real water, authored lighting, photoreal assets where needed,
and a promotion path for public media. The goal is not to keep making the old pixel gates green;
those gates are now treated as historical diagnostics unless a phase explicitly revalidates them.

## Hard Constraints

1. Do not add new `missing_*` hard-fail pixel proxy gates to `tools/scene_graphics_gate.py`.
2. Do not add new overlay/card/proof-scatter passes whose purpose is only to satisfy a pixel
   statistic.
3. Do not call machine-green renders AAA; visual quality remains human/judge gated.
4. Generated high-quality gallery stills may use sequential high-cost capture on the 3070 Ti,
   but do not run parallel renders.
5. Do not publish or promote files from `build/bin/logs`; public media must come through the
   curation pipeline.

## Session Protocol

1. Read `CURRENT.md`, then `QUEUE.md`, then `PLAN.md`.
2. Run `git status -sb` and `git log --oneline -5`.
3. If the tree is dirty, the first action is `tools/run_genscene_acceptance.ps1 -Tag <tag>`.
   Green means commit the scoped checkpoint. Red means fix or revert before new feature work.
4. The runner is the only acceptance path. Hand-run gate fragments are evidence, not accepted
   state.
5. Stay on the first unfinished queue item unless it is recorded as blocked or rejected in
   `QUEUE.md`.
6. Stop after a HUMAN-GATE, a hard external blocker, or an empty queue. Do not stop after a local
   technical win if queue items remain.

## Architecture Facts

- `tools/scene_gen.py` owns prompt-to-render orchestration and writes render artifacts under
  `build/bin/logs`.
- `tools/scene_quality_gate.py` is the semantic/color/focal gate and remains useful.
- `tools/scene_graphics_gate.py` currently contains the historical Goodhart tower. Treat most
  image statistics as telemetry until the gate is redesigned.
- `src/Core/Engine_Scenes.cpp` contains the generated exterior path and the accumulated overlay
  pass stack.
- `assets/shaders/Water.hlsl`, `assets/shaders/MaterialResolve.hlsl`, and
  `assets/shaders/SurfaceClassification.hlsli` are involved in the half-applied Loop 43 shader
  work and must not be accepted without the runner.
- `docs/GENSCENE_NEXT_LEVEL_PLAN.md` is the current strategic review from Claude; it is evidence
  and plan context, not state.

## File Map

- `tools/run_genscene_acceptance.ps1` - acceptance runner.
- `artifacts/genscene_acceptance/<tag>/` - runner artifacts.
- `docs/media/genscene/manifest.json` - intended curated gallery manifest after Phase 1.
