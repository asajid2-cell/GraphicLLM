# Loops: Real Coherent AAA GenScene

`CURRENT.md` wins for state. This file is the active loop contract and progress ledger for
the post-Goodhart GenScene campaign.

## Grand Goal Contract

The grand goal is real coherent high-fidelity generated scenes, not machine-green proxy
renders. The campaign is done only when all of the following are true:

- The old overlay/gate-ratchet campaign is closed: no dirty production diff from Loop 43,
  no new `missing_*` graphics hard gates, and the acceptance runner writes green state.
- Public media is curated: generated stills promoted into `docs/media/genscene/` have
  human filenames, canonical PNG encoding, and manifest records with prompt/seed/settings.
- The graphics gate is reset: semantic correctness stays hard, render health stays hard,
  old edge/flatness/card statistics are telemetry, and visual quality is judge/human gated.
- Generated exteriors use structural scene systems for the visible scene: continuous
  terrain, subsystem water, authored sky/light/fog, coherent composition, and photoreal
  assets where the prompt demands realism.
- Three canonical prompts produce best-of-N stills that are semantically correct and
  visually coherent:
  - `a foggy mountain campsite beside a purple lake at dawn`
  - `a stormy alpine lake with a small cabin and blue moonlight`
  - `a sunny desert canyon campsite with red rocks and a turquoise river`
- HUMAN-GATE: the final curated stills must pass the user's eye as a serious AA/AAA push.
  Autonomous loops may not claim this gate by pixel statistics.

## Baseline

- Accepted state: see `CURRENT.md`.
- Latest rejected state: see `CURRENT_FAILED.md`.
- Strategy: see `PLAN.md`.
- Queue and accept/reject log: see `QUEUE.md`.
- Heartbeat proof: `node z:/328/CMPUT328-A2/codexworks/301/heartbeat/bin/hb.mjs wait --label genscene-aaa-coherent --timeout 1 --poll 1` fired by timeout on 2026-07-06.

## Verifier Registry

| Verifier | Trust | What It Proves | Proof |
|---|---|---|---|
| `tools/run_genscene_acceptance.ps1` | partial | Phase 0 state is clean, no new `missing_*` ratchet diff is dirty, Python tools compile, Release builds | Proven red on dirty Loop 43 diff with `phase0_dirty_overlay_audit_c_20260706`; green still pending |
| `scene_quality_gate.py` semantic checks | trusted historical | Known-bad prompt artifacts are rejected for semantic/color/focal failures | Retained from prior campaign; will be re-run in Phase 2 |
| render health checks | planned | Renders do not crash, timeout, black-frame, or miss frame sidecars | To be codified during gate reset |
| held-out visual judge rubric | planned | Coherence, composition, material believability, lighting, artifact scan | Required before claiming visual loop completion |
| curated gallery manifest check | planned | Public media entered through the promotion pipeline only | To be built in Phase 1 |

## Loop Contracts

### Loop 0: Freeze And Checkpoint

Invariant: the abandoned overlay campaign is no longer live code, and the new campaign-state
scaffold is committed behind a green runner state.

Scope in:

- `.gitignore`
- `HANDOFF.md`, `PLAN.md`, `QUEUE.md`, `LOOPS.md`, `CURRENT.md`, `CURRENT_FAILED.md`
- `tools/run_genscene_acceptance.ps1`
- archive banners on superseded ledgers

Scope out:

- New scene visuals
- New graphics gates
- New runtime overlay passes
- Edits to shader/engine/compiler files from Loop 43

Verifier:

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\run_genscene_acceptance.ps1 -Tag phase0_<tag>_20260706`
- Expected red before rejection: dirty old production diff fails `clean_tree`,
  `gate_ratchet_freeze`, and `phase0_policy`.
- Expected green after checkpoint bootstrap: `clean_tree`, `gate_ratchet_freeze`,
  `python_compile`, `release_build`, and `phase0_policy` pass.

Exit:

- Rejected production diff is backed up under ignored `artifacts/genscene_acceptance/`.
- Runner writes green `CURRENT.md`.
- Commit contains only the accepted scaffold/state transition.

Escape:

- If Release build fails after reverting Loop 43, diagnose build break before any new feature work.

Status: done. Evidence: `tools/run_genscene_acceptance.ps1 -Tag phase0_clean_freeze_20260706`
wrote green `CURRENT.md`; `clean_tree`, `gate_ratchet_freeze`, `python_compile`,
`release_build`, and `phase0_policy` all passed.

### Loop 1: Curated Media Pipeline

Invariant: no generated scene is promoted from `build/bin/logs` directly. Promotion creates a
canonical PNG, stable human filename, and manifest entry.

Scope in:

- `.gitignore`
- `tools/curate_gallery.py`
- `docs/media/genscene/manifest.json`
- small manifest tests or dry-run checks

Scope out:

- Renderer changes
- Visual quality claims
- Bulk publishing loop artifacts

Verifier:

- Curator dry run rejects missing source PNGs and loop-code destination names.
- Curator promotes one existing PNG into `docs/media/genscene/tmp` or final gallery with
  manifest metadata.
- No files under `docs/media/final_art/` or `build/bin/logs/` are staged.

Status: done. Evidence: `tools/run_genscene_acceptance.ps1 -Tag phase1_curation_20260706`
wrote green `CURRENT.md`; `curation_gate` proved bad loop-style ids fail, good `genscene_*`
dry-runs pass, the manifest is valid, debris paths are ignored, and no generated debris is tracked.

### Loop 2: Gate Reset

Invariant: `scene_graphics_gate.py` stops acting as a 48-code Goodhart tower. Pixel statistics
become telemetry unless they are small render-health checks.

Scope in:

- `tools/scene_graphics_gate.py`
- `tools/scene_quality_gate.py` only for integration, not semantic weakening
- gate documentation and acceptance runner extensions

Scope out:

- Adding new `missing_*` pixel proxy hard gates
- Runtime scene pass changes to satisfy telemetry

Verifier:

- Existing known-bad semantic fixtures still fail.
- Render-health fixture catches black/missing-sidecar failures.
- Old edge/flatness/card statistics are emitted as telemetry without hard failure.

Status: pending.

### Loop 3: Continuous Terrain And Real Water

Invariant: generated exterior ground and lakes/rivers are built from structural terrain and
the existing water subsystem, not foreground card stacks or color rectangles.

Scope in:

- Generated exterior compiler/runtime terrain and water routing
- Existing heightfield/FBM and `WaterSubsystem` integration
- Three canonical prompt renders

Scope out:

- More overlay cards
- New pixel proxy ratchets
- Parallel GPU stress runs

Verifier:

- Runtime receipts prove terrain mesh and subsystem water path.
- Three canonical prompts render sequentially and pass semantic plus render-health gates.
- Visual judge/human gate is required before claiming the loop visually done.

Status: pending.

### Loop 4: Authored Lighting, Materials, Assets, Composition

Invariant: scenes are art-directed shots with believable material response, scale, lighting,
atmosphere, and photoreal asset choices where needed.

Scope in:

- Director IR lighting/material/camera fields
- asset ladder and catalog tags
- composition rules
- high-quality sequential still capture settings

Scope out:

- Kenney-only realism claims
- one-prompt filename hacks
- hidden verifier weakening

Verifier:

- Canonical prompts have manifest-backed best-of-N candidates.
- Judge rubric reports per-axis verdicts; any veto keeps the loop open.
- User/HUMAN-GATE decides whether the result is actually good enough.

Status: pending.

### Loop 5: Synthesis

Invariant: all completed loops integrate into a real prompt-to-curated-still workflow.

Verifier:

- Fresh run from prompt to curated gallery entry on all three canonical prompts.
- Acceptance runner green.
- No stale docs contradict `CURRENT.md`.
- Residual risks and HUMAN-GATE items are explicit.

Status: pending.

## Progress

- 2026-07-06: Opened the post-Goodhart loop ledger after Claude review. Loop 0 is active.
- 2026-07-06: Loop 0 accepted. Rejected Loop 43 production diff, committed the campaign-state
  scaffold, then ran `tools/run_genscene_acceptance.ps1 -Tag phase0_clean_freeze_20260706`
  from a clean tree. The runner wrote green `CURRENT.md`.
- 2026-07-06: Loop 1 curation implementation added `tools/curate_gallery.py`, an empty
  `docs/media/genscene/manifest.json`, staging promotion mode, and a runner `curation_gate`.
  Dirty probe `phase1_curation_dirty_probe_20260706 -SkipBuild` passed `curation_gate`,
  `python_compile`, and `gate_ratchet_freeze`; it failed only the expected dirty-tree gates.
- 2026-07-06: Loop 1 accepted. Full runner `phase1_curation_20260706` passed clean tree,
  ratchet freeze, Python compile, curation gate, Release build, and phase0 policy.
