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
- 2026-07-06: Loop 4 iteration 6 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_shadow_water_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural
  scene gates, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_renderer_shadow_water_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; next Loop 4 work
  must tackle real asset/composition/material form, because renderer-owned shadow/water cleanup
  alone still leaves low-poly hero props and sheet-like lakes.
- 2026-07-06: Loop 4 iteration 7 first dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_asset_form_dirty_probe_20260706`
  built Release and passed ratchet freeze, Python compile, curation, graphics reset, campsite
  and alpine structural gates, but failed desert `scene_quality_gate.py` on
  `turquoise_water_roi_fail` after the river was narrowed. This was rejected as a real semantic
  regression, not a gate problem. Visual inspection also showed the primitive camp-detail
  emitters were still producing stick/slab clutter.
- 2026-07-06: Loop 4 iteration 7 second dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_asset_form_dirty_probe2_20260706`
  re-armed heartbeat every 60s until PID exit, built Release, and passed ratchet freeze,
  Python compile, curation, graphics reset, and all structural scene gates. It failed only
  expected dirty-tree policy gates. The implementation demotes the large catalog tent to a small
  semantic marker, removes loose log/canoe clutter, reduces stylized pine/cliff counts, boosts
  naturalistic/source anchors, zeros campsite primitive detail emitters and low-poly mask paths,
  keeps one tent construction path, narrows structural water near shore, and restores turquoise
  river readability. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: cleaner, but still
  primitive tent/cabin silhouettes, luminous sheet water, flat ridge backdrops, and non-AAA
  material form.
- 2026-07-06: Loop 4 iteration 8 dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_grounded_composition_material_dirty_probe_20260706 -SkipBuild`
  ran after an explicit VsDevCmd-backed Release build of the dirty C++ diff. Heartbeat
  `genscene-grounded-composition-dirty-probe` re-armed on 60s beats and then fired on PID exit.
  The diff adds curved shore-bank geometry that follows the generated water outline, replaces the
  solid tent wedge with a subdivided canvas mesh and dark entrance opening, moves campsite/desert
  default camera framing toward the hero cluster, reduces water light-card alpha/emissive strength,
  fixes dirt terrain tint after texture binding, and raises only canyon ambient/exposure to avoid
  crushed foregrounds. The probe passed ratchet freeze, Python compile, curation, graphics reset,
  and structural scene gates, and failed only expected dirty-tree policy gates. Selected candidates:
  campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: composition and
  tent/ground material read are better, but water is still broad, mountains remain flat bands, and
  desert still selects a dark brown frame because brighter candidates wash out the turquoise ROI.
- 2026-07-06: Loop 4 iteration 8 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_grounded_composition_material_20260706`
  passed clean tree, ratchet freeze, Python compile, curation, graphics reset, Release build,
  structural scene gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_grounded_composition_material_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; next Loop 4 work
  must leave the current camera/material cleanup lane and attack still-flat water surface response,
  cardboard ridge/backdrop geometry, and real asset-library replacement.
- 2026-07-06: Loop 4 iteration 9 dirty probe attacked real water/backdrop systems instead of
  proxy overlays. The diff reshapes generated lakes into cove/tapered water meshes, gives
  procedural ridge layers actual depth/faceted normals, lowers authored water tint-as-emission,
  makes the water shader derive depth from shore-to-far UV rather than treating every edge as
  shallow, and restores desert turquoise readability with a river-only palette correction.
  Heartbeats `genscene-water-ridge-build3`, `genscene-water-ridge-build4`,
  `genscene-water-ridge-canonical-renders2`, and `genscene-water-ridge-dirty-probe` fired during
  build/render/probe waits. Manual Release builds passed, canonical focused renders stayed valid
  after correcting a real `turquoise_water_roi_fail`, and dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_water_ridge_depth_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  failing only expected dirty-tree policy gates. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: water is less
  neon and ridges are less purely flat, but scenes still read staged/disconnected and not AA/AAA.
- 2026-07-06: Loop 4 iteration 9 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_water_ridge_depth_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_water_ridge_depth_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; Loop 4 stays open.
  Next work must move beyond water/backdrop material tuning into real asset replacement,
  coherent terrain/composition staging, and stronger lighting/shadow authoring.
- 2026-07-06: Loop 4 iteration 10 dirty probe moved campsite staging toward source-backed
  coherent clusters. The authored campsite module now loads optional scanned Poly Haven/local
  meshes for dead trunks, branches, boulders, lanterns, and a wood table; uses an irregular
  terrain patch for the camp pad/fire patch instead of rectangular slabs; and falls back to the
  old primitives only when source meshes fail to load. Focused campsite render stayed hard-valid
  and the critic selected a `good` candidate, but visual truth remains negative because scattered
  debris and shore strip artifacts are still visible. Manual Release build passed under heartbeat
  `genscene-source-cluster-build`. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_cluster_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  failing only expected dirty-tree policy gates. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`.
- 2026-07-06: Loop 4 iteration 10 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_cluster_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_source_cluster_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; next work should
  clean the remaining strip/debris artifacts and continue replacing weak hero/background forms.
- 2026-07-06: Loop 4 iteration 11 dirty probe cleaned one shore-strip/readability failure without
  adding gates or proxy overlays. The diff disables the old authored shore cube slabs whenever
  real curved shore layers exist, pulls canyon rivers slightly into the midground water band as
  actual water, boosts only canyon-river `WaterSurfaceComponent` authored color/opacity so the
  river remains turquoise after terrain/reflection blending, and makes canyon wet-shore transition
  patches darker/shorter instead of bright `terrain_shore` bands. Heartbeats
  `genscene-shore-strip-build3`, `genscene-shore-strip-build4`, and
  `genscene-shore-strip-dirty-probe3` fired during build/probe waits. Rejected probes 1-2 found a
  real desert `turquoise_water_roi_fail`; dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shore_strip_cleanup_dirty_probe3_20260706 -SkipBuild`
  then passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  failing only expected `clean_tree` plus `phase0_policy`. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`; desert water ROI improved to avg RGB
  `[0.5259, 0.5893, 0.6013]` and turquoise fraction `0.432`. Visual truth remains negative:
  pale strip artifacts and primitive staging are reduced but still visible, so this is only a
  bounded shoreline/river-readability checkpoint.
- 2026-07-06: Loop 4 iteration 11 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shore_strip_cleanup_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_shore_strip_cleanup_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; next active work
  should attack remaining strip/clutter artifacts and weak hero/background form, not declare this
  a quality solve.
- 2026-07-06: Loop 4 iteration 12 dirty probe removed another visible-card path instead of
  adding new proxy overlays. The compiler now requests zero hero material panels, zero hero
  shadow receivers, zero volumetric light slices, and zero wet roughness variation patches,
  while reducing cinematic relief/shadow caster counts and relabeling those systems as
  renderer-owned material/contact/fog work. The first dirty probe found a real campsite
  `purple_water_roi_fail`; the compiler then strengthened purple/violet lake material shallow
  and deep colors plus `color_strength` so the prompt color remains readable through the real
  water path. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_visible_card_cleanup_dirty_probe2_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  failing only expected `clean_tree` plus `phase0_policy`. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Campsite purple ROI passed with average RGB
  `[0.5183, 0.4412, 0.5091]`, average saturation `0.2183`, and purple fraction `0.6332`.
  Visual truth remains negative: this is only a visible-card subtraction checkpoint. Remaining
  orange rectangular ghosting likely comes from emissive/practical/bloom behavior, not the
  now-zeroed compiler card budgets.
- 2026-07-06: Loop 4 iteration 12 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_visible_card_cleanup_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural
  scene gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_visible_card_cleanup_20260706`. Visual truth remains negative; Loop 4 stays open.
  Next active work should diagnose the remaining orange rectangular ghosting and replace weak
  hero/background forms through real emissive, bloom, material, asset, and lighting systems.
- 2026-07-06: Loop 4 iteration 13 dirty probe attacked the remaining orange rectangular
  ghosting through real emissive/bloom controls, not compiler cards. The engine now supports
  per-material `emissiveBloomFactor` for asset-led materials, generated exterior bloom intensity
  and bloom max contribution are lower, large cabin warm-light spill mesh was removed, cabin
  window/campfire visible emissive geometry was reduced, and warm point lights were kept as the
  actual illumination path. Probe 1 passed the real gates but left a visible campsite ember
  block, so the fire core was shrunk and dimmed before probe 2. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_emissive_bloom_cleanup_dirty_probe2_20260706`
  passed ratchet freeze, Python compile, curation, graphics reset, Release build, and structural
  scene gates, failing only expected `clean_tree` plus `phase0_policy`. Selected candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Visual truth remains negative: alpine glow bars
  are much cleaner, but water/shore strip artifacts, billboard ridges, and primitive hero forms
  still dominate the quality failure.
- 2026-07-06: Loop 4 iteration 13 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_emissive_bloom_cleanup_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural
  scene gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_emissive_bloom_cleanup_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; Loop 4 stays
  open. Next active work should target water/shore band geometry, backdrop massing, and
  primitive hero/background forms rather than more bloom tuning.
- 2026-07-06: Loop 4 iteration 14 dirty probe attacked visible noise and strip artifacts without
  adding gates or proxy overlays. The compiler now requests zero visible haze depth bands,
  horizon blend bands, terrain macro breakup cards, and directional shadow lanes; shoreline props
  are fewer and placed on flanks; primitive tree silhouettes are disabled; Kenney/source pine
  backdrop trees are reduced and pushed farther back; generated exteriors now use much lower
  renderer particle density and scale down the recipe dust emitter. Renderer-side backstops keep
  stale IR environment/haze/water/reflection/shadow-lane counts non-visible. Heartbeats
  `genscene-visible-band-dirty-probe` and `genscene-visual-noise-dirty-probe` fired during the
  probe runs. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_visual_noise_cleanup_dirty_probe_20260706`
  passed ratchet freeze, Python compile, curation, graphics reset, Release build, and structural
  scene gates, failing only expected `clean_tree` plus `phase0_policy`. Selected candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: visible speckle
  and some strip clutter are reduced, but flat sheet water, primitive tent/cabin forms, and
  backdrop/asset quality still block a serious AA/AAA result.
- 2026-07-06: Loop 4 iteration 14 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_visual_noise_cleanup_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural
  scene gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_visual_noise_cleanup_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Visual truth remains negative; Loop 4 stays
  open. Next active work should replace weak tent/cabin/shore hero forms and deepen water-surface
  material response instead of more strip/noise cleanup.
- 2026-07-06: Loop 4 iteration 15 dirty probe shifted hero/water form toward real geometry.
  The compiler no longer emits the catalog `tent_smallClosed` wedge for generated campsites;
  procedural canvas shell/seam/pole/rope detail now owns the tent silhouette. The generated
  water mesh now curves the near shore per vertex, adds subtle surface displacement, and exposes
  stronger water normal/procedural/transmission material response before the shader waves. A
  focused campsite render stayed semantic-valid before the full probe. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_water_form_dirty_probe_20260706` passed
  ratchet freeze, Python compile, curation, graphics reset, Release build, and structural scene
  gates, failing only expected `clean_tree` plus `phase0_policy`. Selected candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Visual truth remains negative: the tent is less
  blocked by a catalog slab and the river/lake edge is less rigid, but the scenes still lack
  convincing material depth, water shading, and high-quality backdrop/assets.
- 2026-07-06: Loop 4 iteration 15 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_water_form_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_hero_water_form_20260706`. Visual truth remains negative; Loop 4 stays open. Next
  active work should attack lighting/material/backdrop cohesion and water-surface depth rather
  than adding proxy cards or more pixel gates.
- 2026-07-06: Loop 4 iteration 16 dirty probe attacked the remaining flat water-sheet read
  through real geometry rather than overlays. The first probe exposed white gaps after removing
  the full-width tinted seabed, so it was stopped as stale evidence. The second probe adds a
  neutral far-shore terrain floor, gives the underwater bed the same curved lake/river footprint
  as the water mesh, and tightens water-body width/resolution. Heartbeats
  `genscene-shaped-waterbed-probe2-build`, `genscene-shaped-waterbed-probe2-render`,
  `genscene-shaped-waterbed-probe2-alpine-desert`, and
  `genscene-shaped-waterbed-probe2-final` fired during build/render waits. Dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_waterbed_dirty_probe2_20260706` passed
  ratchet freeze, Python compile, curation, graphics reset, Release build, and structural scene
  gates, failing only expected `clean_tree` plus `phase0_policy`. Selected candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: bounded water
  reads better, but front shore edges, low-fidelity backdrop masses, lighting, and hero assets
  still block a coherent AA/AAA result.
- 2026-07-06: Loop 4 iteration 16 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_waterbed_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_shaped_waterbed_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Visual truth remains negative; Loop 4 stays
  open. Next work should target coherent backdrop/terrain/lighting form, not more water-footprint
  cleanup unless it blocks a real prompt.
- 2026-07-06: Loop 4 iteration 17 rejected the foothill/backdrop dirty probe
  `phase4_foothill_backdrop_dirty_probe_20260706`. It passed the automated dirty-probe gates
  except expected dirty-tree policy, but the selected stills did not improve the image: desert
  gained broad pale shelf bands behind the water, campsite/alpine stayed cardboard-flat, and the
  patch added another ridge-like slab system instead of removing the visual ceiling. Reverted the
  production diff and dirty `CURRENT_FAILED.md` change. Strategy review: this is exactly the
  local-minimum failure Claude called out, so the next slice must replace backdrop/terrain form
  through continuous terrain/material/lighting systems or remove weak geometry, not add another
  metric-friendly layer.
- 2026-07-06: Loop 4 iteration 18 dirty probe cleaned composition and visible clutter without
  adding gates. It zeros the remaining structural terrain/material patch emitters and backdrop
  detail ridge accretion, cuts random ecology/source-placement budgets, narrows water footprints,
  lifts purple water material enough to keep the prompt read, and uses tighter module-specific
  cameras for campsite, cabin, and canyon. Probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_declutter_camera_dirty_probe2_20260706` passed
  ratchet freeze, Python compile, curation, graphics reset, Release build, and structural scene
  gates, failing only expected dirty-tree policy. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: this is a
  cleanup/framing checkpoint, not the AAA solve. Next work should replace the remaining flat
  water/shore and cardboard backdrop/hero geometry with real material/terrain/asset systems.
- 2026-07-06: Loop 4 iteration 18 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_declutter_camera_20260706` passed clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_declutter_camera_20260706`. Accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; Loop 4 stays
  open for water/shore material depth, backdrop mass replacement, and hero asset fidelity.
- 2026-07-06: Loop 4 iteration 19 dirty probe targeted existing backdrop material depth, not new
  geometry or new gates. The first probe proved the material path reached the renderer but was
  visually rejected because boulder-scale cliff textures crushed the desert backdrop into dark
  charcoal massing. The adjusted probe routes procedural ridge layers through terrain-scale
  `terrain_rock` / `terrain_sand` texture sources, gives ridge/backdrop surfaces a capped
  material-response pass, and adds runtime receipts:
  `backdrop material depth textured_surfaces=2 ridge_layers=2` plus
  `lighting shadow material field ... backdrop_surfaces=8`. Probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_backdrop_material_depth_dirty_probe2_20260706`
  passed ratchet freeze, Python compile, curation, graphics reset, Release build, and structural
  scene gates, failing only expected dirty-tree policy. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: campsite/desert
  ridges read less like flat color cards, but water sheets, foreground/shore material, and hero
  asset fidelity still block coherent AA/AAA quality.
- 2026-07-06: Loop 4 iteration 19 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_backdrop_material_depth_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene
  gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_backdrop_material_depth_20260706`. Accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; this checkpoint
  only lands real material response on existing ridge/backdrop surfaces. Loop 4 stays open for
  real water/shore shading and hero asset/material fidelity.
- 2026-07-06: Loop 4 iteration 20 rejected the water transmission/depth slice after dirty probes
  `phase4_water_transmission_depth_dirty_probe_20260706` through
  `phase4_water_transmission_depth_dirty_probe6_20260706`. Probe 1 passed the automated dirty
  gates except expected dirty-tree policy, but manual inspection still showed flat colored water
  sheets. Probes 2-6 then oscillated between `purple_water_roi_fail` and
  `turquoise_water_roi_fail` while trying to preserve prompt color through alpha/transmission,
  optical tint compression, and shader chroma bias. This hit the loop escape rule: same visual
  failure class, no structural improvement. Reverted `assets/shaders/Water.hlsl`,
  `src/Core/Engine_Scenes.cpp`, and the dirty `CURRENT_FAILED.md` state. Next work should not
  continue water-color ratio tuning; move to a higher-leverage front such as hero asset/material
  replacement, real shore/terrain geometry, or composition/lighting systems that can change the
  actual image structure.
- 2026-07-06: Loop 4 iteration 21 started as a hero-cluster grounding slice. Baseline stills
  inspected from accepted tag `phase4_backdrop_material_depth_20260706`: campsite still reads as
  an isolated toy A-frame on broad empty ground; alpine cabin has a stronger focal object but
  low-detail surrounding scatter; desert canyon campsite crops the tent and leaves the fire/props
  disconnected from the river. Scope is limited to existing compiler/runtime hero, authored
  source-asset, material, light, and camera composition paths. Kill criteria: reject if the slice
  adds new pixel hard gates, resumes water color-ratio tuning, creates more visible strips/slabs,
  or keeps the campsite/desert hero set visibly disconnected after the dirty probe.
- 2026-07-06: Loop 4 iteration 21 rejected after dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_cluster_grounding_dirty_probe2_20260706`.
  Heartbeat `genscene-hero-cluster-dirty-probe2` fired on timeout while the runner continued.
  The runner passed ratchet freeze, Python compile, curation, graphics reset, Release build, and
  structural scene gate, and failed only expected dirty-tree policy gates. Selected candidates
  were campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Manual visual inspection rejected the slice
  against its own kill criteria: campsite/desert still read as disconnected low-fidelity tent and
  prop clusters in front of flat water/backdrop forms, so accepting it would continue the
  incremental local-minimum pattern. Revert the production diff and dirty `CURRENT_FAILED.md`;
  next work must be a higher-leverage system front, not another hero-clutter tweak.
- 2026-07-06: Loop 4 iteration 22 started as a renderer/material coherence slice. Recon found
  `lighting_shadow_material_field` sets stronger renderer-owned SSAO/shadows/contact lighting,
  then `source_readability_balance` runs later and lifts ambient/exposure/IBL while reducing
  effective SSAO, which plausibly explains the flat washed generated stills after the stronger
  lighting pass. Scope is limited to the final renderer settings and material-readability
  behavior in `src/Core/Engine_Scenes.cpp`; no compiler prompt changes, no new gates, no new
  meshes/cards/overlays. Hypothesis: preserving stronger SSAO/shadow settings and reducing the
  late ambient/exposure washout improves depth/coherence without hurting prompt readability.
  Kill criteria: reject if any semantic/structural gate fails, if water color ROI regresses, if
  the images become crushed/dark, if a new pixel hard gate appears, or if the slice adds visible
  proof geometry instead of renderer/material behavior.
- 2026-07-06: Loop 4 iteration 22 dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_material_coherence_dirty_probe_20260706 -SkipBuild`
  after manual Release build heartbeat `genscene-renderer-material-build2` passed. Probe heartbeat
  `genscene-renderer-material-dirty-probe` fired on timeout while the runner continued. The runner
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gate,
  with Release build recorded as skipped after the manual build; it failed only expected
  dirty-tree policy gates. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual read: not AA/AAA and still below the
  human gate, but contact/material depth is less washed and prompt water colors remain readable;
  no new geometry, no new gates, no water-ratio tuning. Accept only as a bounded renderer/material
  coherence checkpoint, then continue with larger structural fronts.
- 2026-07-06: Loop 4 iteration 22 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_material_coherence_20260706` passed
  clean tree, ratchet freeze, Python compile, curation, graphics reset, Release build,
  structural scene gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_renderer_material_coherence_20260706`. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: this is only a
  renderer-contact/material-depth cleanup. Next active item must be a larger structural front:
  real continuous terrain/backdrop replacement, water/shore integration, or photoreal asset
  replacement; do not spend the next loop on another small exposure/ambient tweak.
