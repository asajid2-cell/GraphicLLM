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

Status: done. Evidence: `tools/run_genscene_acceptance.ps1 -Tag phase2_gate_reset_20260706`
wrote green `CURRENT.md`; the reset gate has zero `missing_` hard codes, known-bad fixtures
fail render-health expect-fail, and sidecar-backed pipeline fixtures pass.

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

Status: done. Evidence: `tools/run_genscene_acceptance.ps1 -Tag phase3_structural_terrain_water_20260706`
wrote green `CURRENT.md`; campsite, alpine, and desert direct quality + graphics gates passed,
and all three logs contained shared-FBM terrain plus `WaterSurfaceComponent` water receipts.
Visual truth remains negative and is carried into Loop 4.

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

Status: running. Iteration 1 evidence: overlay/proof-debris subtraction reduced compiler
budgets for contact cards, foam strips, ground decals, material-response cards, and synthetic
shadow bands without adding hard pixel gates. Direct canonical prompts rendered sequentially
and passed semantic plus render-health gates, but visual truth remains negative: water is still
sheet-like, asset silhouettes remain kit-like, and foreground debris is still visible.
Iteration 2 evidence: generated water now uses a procedural shaped mesh (`curved_lake_cove` or
`s_curve_river`) under `WaterSurfaceComponent` instead of a single rectangle, and environment
water/reflection overlay bands are disabled. Direct canonical gates passed after widening the
river enough to preserve the turquoise semantic read.
Iteration 3 evidence: composition controls no longer force foreground/water-strip minimums, and
compiler budgets zero micro-detail overlays and foreground dressing clusters. Canonical gates
remain green, with campsite selector producing a `good` frame, but visual truth remains below
target because hero/camp material geometry is still busy and stylized.

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
- 2026-07-06: Loop 2 implementation replaced the old hard graphics-fidelity tower with a
  401-line render-health gate. `tools/scene_graphics_gate.py` now contains zero `missing_`
  codes; image and old graphics-pass signals are telemetry. Dirty probe
  `phase2_gate_reset_dirty_probe_20260706 -SkipBuild` passed `graphics_gate_reset`,
  curation, Python compile, and ratchet freeze, failing only expected dirty-tree gates.
- 2026-07-06: Loop 2 accepted. Full runner `phase2_gate_reset_20260706` passed clean tree,
  ratchet freeze, Python compile, curation gate, graphics gate reset, Release build, and
  phase0 policy.
- 2026-07-06: Loop 3 implementation wired generated terrain to shared
  `Scene::SampleTerrainHeight` FBM params, added file-backed IR handoff to avoid Windows env-var
  limits on dense scenes, strengthened canyon turquoise material dominance, and added runtime
  receipts for shared-FBM terrain plus `WaterSurfaceComponent` water. Three canonical prompts
  rendered sequentially; direct quality and graphics gates passed for campsite, alpine, and
  desert, and all three logs contain terrain/water structural receipts. Visual truth remains
  negative: the scenes still show broad water sheets, patch debris, and kit silhouettes.
  Dirty probe `phase3_structural_terrain_water_dirty_probe_20260706 -SkipBuild` passed the new
  `structural_scene_gate` and failed only expected dirty-tree gates.
- 2026-07-06: Loop 3 accepted. Full runner `phase3_structural_terrain_water_20260706`
  passed clean tree, ratchet freeze, Python compile, curation, graphics reset, structural
  scene gate, Release build, and phase0 policy.
- 2026-07-06: Loop 4 iteration 1 implemented subtraction instead of new overlays: reduced
  `scene_compiler.py` budgets for contact patches, shore layers, deep contact cards, foam/ripple
  strips, ground decals, material-response cards, and shadow bands. `python -m py_compile`
  passed for the compiler/generator/gates. Three canonical prompts rendered sequentially:
  campsite best `build/bin/logs/gen_a_foggy_mountain_campsite_beside_2.png`, alpine best
  `build/bin/logs/gen_a_stormy_alpine_lake_with_a_smal_0.png`, desert best
  `build/bin/logs/gen_a_sunny_desert_canyon_campsite_w_2.png`. Direct `scene_quality_gate.py`
  and `scene_graphics_gate.py` passed for all three, including desert turquoise ROI
  `turquoise_fraction=0.3981`. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_overlay_subtraction_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  and failed only expected dirty-tree gates. Visual truth remains negative; next Loop 4 work
  must be structural lighting/material/composition, not another debris-count tweak.
- 2026-07-06: Loop 4 iteration 1 accepted as a cleanup checkpoint. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_overlay_subtraction_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, structural scene gate, Release build,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_overlay_subtraction_20260706`.
- 2026-07-06: Loop 4 iteration 2 replaced rectangular generated water with
  `CreateGenerativeWaterBodyMesh`, a procedural XZ mesh with cove width modulation for lakes and
  an S-curve canyon channel for rivers. Compiler environment water/reflection overlay bands were
  set to zero, with terrain/shadow overlay counts reduced, so water shape comes from geometry
  instead of translucent proof strips. Release build passed after editing. Canonical renders:
  campsite `build/bin/logs/gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `build/bin/logs/gen_a_stormy_alpine_lake_with_a_smal_2.png`, desert
  `build/bin/logs/gen_a_sunny_desert_canyon_campsite_w_2.png`. Quality and graphics gates passed
  for campsite/alpine; desert first failed `turquoise_water_roi_fail` at `turquoise_fraction=0.3515`
  after narrowing, then passed after widening the shaped river to `turquoise_fraction=0.4073`.
  Receipts include `shape=curved_lake_cove` and `shape=s_curve_river`, 539 vertices, 960
  triangles, and `WaterSurfaceComponent`. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_water_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  and failed only expected dirty-tree gates. Visual truth remains negative; this removes one
  structural flat-sheet failure but does not solve asset/material/composition quality.
- 2026-07-06: Loop 4 iteration 2 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_water_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, structural scene gate, Release build,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag `phase4_shaped_water_20260706`.
- 2026-07-06: Loop 4 iteration 3 reduced forced composition clutter. Compiler budgets now set
  foreground occluders to side anchors, authored foreground frames to 1-2, water shape accents
  to 2, foreground dressing clusters to 0, and surface pebbles/creases/wet glints to 0. Runtime
  authored-module code now honors exact `foreground_frame_count` and `water_shape_segment_count`
  instead of forcing minimums of 3 and 6. Release build passed. Canonical selected renders:
  campsite `build/bin/logs/gen_a_foggy_mountain_campsite_beside_1.png` (selector score 4,
  verdict `good`), alpine `build/bin/logs/gen_a_stormy_alpine_lake_with_a_smal_0.png`, and
  desert `build/bin/logs/gen_a_sunny_desert_canyon_campsite_w_2.png`. Direct quality and graphics
  gates passed for all three; desert turquoise ROI held at `turquoise_fraction=0.4063`.
  Dirty probe `tools/run_genscene_acceptance.ps1 -Tag phase4_composition_cleanup_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  and failed only expected dirty-tree gates. Visual truth remains negative; next work should
  target hero mesh/material replacement and actual material response, not more count trimming.
- 2026-07-06: Loop 4 iteration 3 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_composition_cleanup_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, structural scene gate, Release build,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_composition_cleanup_20260706`.
- 2026-07-06: Loop 4 iteration 4 reduced hero/material overpaint counts instead of adding
  more proof geometry, and fixed candidate selection so color-failing critic results cannot
  win equal-score ties. The first dirty probe
  `phase4_hero_material_cleanup_dirty_probe2_20260706 -SkipBuild` failed
  `structural_scene_gate` because the runner was hardcoded to stale desert candidate `_2`;
  direct gating showed the selected `_0` candidate passed semantic and graphics checks.
  The runner verifier was then corrected to render each canonical prompt fresh, parse
  `best render:`, and gate that selected candidate without weakening semantic/render-health
  checks. Dirty probe
  `phase4_hero_material_cleanup_dirty_probe3_20260706 -SkipBuild` selected campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_2.png`; all passed quality, graphics, and
  terrain/water receipt checks. The probe failed only expected dirty-tree policy gates.
  Visual truth remains negative; next work must move to real lighting/material/asset systems.
- 2026-07-06: Loop 4 iteration 4 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_material_cleanup_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, fresh structural scene
  generation/gating, Release build, and phase0 policy, and rewrote `CURRENT.md` to accepted
  tag `phase4_hero_material_cleanup_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; Loop 4 stays
  open for actual lighting/material/asset replacement.
- 2026-07-06: Loop 4 iteration 5 replaced part of the primitive hero overbuild with source
  assets and tightened verifier freshness. The full runner now performs Release build before
  fresh structural renders so engine changes cannot be judged by a stale binary. The compiler
  increased naturalistic ecology/source anchors, reduced hero asset replacement from large
  primitive stacks to small support budgets, and removed low-poly masks. Runtime source props
  were scaled into the hero cluster, then utility clutter was reduced to one wooden table
  after chair/barrel variants proved visually sloppy. `scene_gen.py` now ranks candidates by
  `scene_quality_gate.py` hard semantic result before critic color/score, while the runner
  still re-gates the selected PNG independently. Dirty probe
  `phase4_source_asset_ladder_dirty_probe6_20260706 -SkipBuild` selected campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`; all passed quality, graphics, and
  terrain/water receipt checks. Receipts show campsite source anchors at 12 and primitive
  campsite overbuild at 15 pieces with zero low-poly masks. Visual truth remains negative:
  this is a structural cleanup/source-asset checkpoint, not a solved AAA scene.
- 2026-07-06: Loop 4 iteration 5 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_asset_ladder_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, build-before-render
  Release build, fresh structural scene gates, and phase0 policy, and rewrote `CURRENT.md`
  to accepted tag `phase4_source_asset_ladder_20260706`. Selected accepted candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; the next
  Loop 4 work should target lighting/water/material coherence and actual shadowing.
- 2026-07-06: Loop 4 iteration 6 dirty probe shifted lighting/water/material work away from
  proxy overlays. The compiler now requests zero contact patches, soft penumbra disks, image
  contact occluders, shadow bands, water ripple/glint strips, and authored water shape cards;
  it increases existing-surface material response and lowers ambient ceilings so real SSAO,
  cascaded shadows, local shadowed lights, probes, and the water subsystem carry the image.
  Runtime skips shadow-band mesh upload when the budget is zero, raises cascade quality for
  generated exteriors, prevents transparent water from casting sun shadows, and increases water
  slosh/meniscus/flow. Heartbeat proof
  `genscene-loop4-renderer-shadows-proof` fired by timeout after 1s. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_shadow_water_dirty_probe_20260706`
  built Release and passed ratchet freeze, Python compile, curation, graphics reset, and
  structural scene gates; it failed only expected dirty-tree policy gates. Selected candidates:
  campsite `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: this is a
  cleanup away from Goodhart overlays, not an AAA-quality solve.
