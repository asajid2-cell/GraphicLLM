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
- 2026-07-06: Loop 4 iteration 23 started as a source-asset dominance slice. Recon found the
  source-environment lane still routes backdrop/terrain replacement through fetched/generated
  crag meshes and optional Kenney pine/cliff assets, while local CC0 naturalistic scans with
  bound PBR textures are already available through `AddAssetLedNaturalisticRenderable`. Scope is
  limited to source-environment asset selection/receipts and compiler metadata; no new pixel
  hard gates, no water color-ratio tuning, no overlay/card proof layers. Hypothesis: replacing
  fetched/Kenney source-environment backdrops with scanned boulder, moss-rock, trunk, stump,
  branch, and bush assets will reduce the toy/proxy read and make material response more
  coherent. Kill criteria: reject if semantic/structural gates fail, water ROI regresses, runtime
  receipts do not show naturalistic dominance, or visual inspection says the images are still
  dominated by proxy/fallback geometry.
- 2026-07-06: Loop 4 iteration 23 dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_asset_dominance_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gate
  after a manual Release build, and failed only expected dirty-tree policy. Heartbeat
  `genscene-source-asset-dirty-probe` fired on process exit after 225s. Runtime receipts on the
  selected campsite/alpine/desert renders show `fetched_rocks=0`, `kenney_cliffs=0`,
  `fallback_total=0`, and `naturalistic_total=15-17` with naturalistic PBR texture uploads in
  the frame reports. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: this is not AA/AAA
  and the images still have flat water/terrain, toy tents, and cardboard backdrop massing, but the
  source-environment lane now uses real scanned assets instead of fetched/Kenney fallback meshes.
  Accept only as a bounded structural source-asset checkpoint, then continue to terrain/water/hero
  composition rather than stopping.
- 2026-07-06: Loop 4 iteration 23 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_asset_dominance_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene
  gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_source_asset_dominance_20260706` at HEAD `5bc2908`. Heartbeat
  `genscene-source-asset-clean-accept` fired on process exit after 240s. Selected accepted
  candidates were campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative; this checkpoint
  only proves source-environment fallback demotion and PBR scan dominance. Next loop must attack
  the larger visible failures: flat water/shore, cardboard terrain/backdrop massing, and toy camp
  hero geometry.
- 2026-07-06: Loop 4 iteration 24 started as an integrated terrain-water continuity slice.
  Baseline visual inspection of the accepted source-asset checkpoint shows the same structural
  failure across campsite, alpine, and desert: a broad flat terrain stage, cutout lake/river
  water, ribbon/silhouette mountain backdrops, and hero props sitting disconnected from the
  landscape. Recon found the engine already emits terrain/water receipts, but the generated
  base uses a land mesh ending on a straight shore, separate far-floor/seabed meshes, fixed-up
  water normals, and old patch-layer systems that can still read as strips. Heartbeat
  `genscene-loop24-proof` was proven by a 2s timeout before long waits. Scope is limited to the
  runtime terrain/water base and compiler pass budgets that request visible strip detail; no new
  pixel hard gates, no water color-ratio tuning, no fetched/Kenney fallback reintroduction, and no
  visible card/proof overlays. Hypothesis: replacing the split flat stage with one continuous
  heightfield/shore/waterbed base and deriving water normals from the actual displaced mesh will
  reduce the cutout-sheet read while preserving prompt color and structural receipts. Kill
  criteria: reject if any acceptance gate fails beyond expected dirty-tree policy, if water ROI
  regresses, if runtime receipts do not prove the integrated base reached the renderer, or if
  manual still inspection shows new shelf/slab artifacts instead of cleaner terrain-water
  continuity.
- 2026-07-06: Loop 4 iteration 24 accepted. Full runner
  `tools/run_genscene_acceptance.ps1 -Tag phase4_integrated_terrain_water_20260706` passed clean
  tree, ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene
  gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_integrated_terrain_water_20260706` at HEAD `4f57ca6`. Heartbeat
  `genscene-loop24-clean-accept-2` fired on process exit after 70s. Selected accepted candidates
  were campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`; structural scene gate reported
  `structural_terrain_receipt=True` and `structural_water_receipt=True` for all three. Visual
  truth remains negative: the terrain-water base is more structurally coherent, but the scenes
  still lack convincing backdrop massing, hero asset fidelity, and high-end lighting/material
  response. Loop 4 remains open; next active item should be a larger visual system front, not
  another small water-color or camera tweak.
- 2026-07-06: Loop 4 iteration 25 started as an artifact-removal and renderer-owned exterior
  quality slice. Orientation trusted `CURRENT.md` at accepted tag
  `phase4_integrated_terrain_water_20260706`; git status was clean on `main`. Heartbeat
  `genscene-loop25-proof` fired on a 2s timeout before long waits. Scope is limited to
  `tools/scene_compiler.py`, `src/Core/Engine_Scenes.cpp`, and ledgers. Hypothesis: demoting
  remaining visible synthetic line/card emitters (rain streak cubes, red-rock strata/crack/ridge
  cubes, source terrain replacement layers, procedural ridge/backdrop detail layers, and shadow
  band overlays) while explicitly routing exterior depth through renderer-owned SSAO, shadows,
  fog, SSR, and optional DXR request receipts will make the selected frames cleaner and more
  coherent without adding metric-target geometry. Kill criteria: reject if semantic/structural
  gates fail, if prompt water ROI regresses, if fallback/Kenney assets return, if runtime receipts
  still show visible artifact emitters, or if manual still inspection shows the scene got emptier,
  flatter, or more obviously synthetic.
- 2026-07-06: Loop 4 iteration 25 rejected after dirty probes
  `phase4_artifact_renderer_lighting_dirty_probe3_20260706` and
  `phase4_artifact_renderer_lighting_dirty_probe4_20260706`. Both probes passed ratchet freeze,
  Python compile, curation, graphics reset, and structural scene gates after manual Release
  builds through the VS dev environment, and failed only the expected dirty-tree policy gates.
  Heartbeat `genscene-loop25-dirty-probe4` fired first on timeout at 180s and then on process
  exit after the runner completed. Manual visual inspection rejected the slice: campsite stayed
  an empty low-fidelity stage with slab backdrops and flat purple water, alpine remained a
  close-up cabin against flat water/ice massing, and desert still showed an obvious long straight
  shore strip plus toy camp geometry. The mountain-massing tweak made probe 3 worse and was
  backed out before probe 4; the remaining artifact-demotion/renderer-receipt slice still did
  not improve the visual truth enough to checkpoint. Production diffs and dirty
  `CURRENT_FAILED.md` were reverted; do not retry this as another emitter-demotion pass. Next
  work must replace visible structure: continuous authored terrain/backdrop composition,
  real hero assets/materials, and renderer lighting integration that changes the image, not
  another receipt-only cleanup.
- 2026-07-06: Loop 4 iteration 26 started as an open-ended shore bank geometry slice. Recon
  found the persistent desert/campsite straight shoreline strip is likely the near cap inside
  `CreateGenerativeShoreBankMesh`: it bridges left and right bank vertices across the entire
  foreground water edge, so the curved side-bank mesh still produces a horizontal tan/peach
  band. Heartbeat `genscene-loop26-proof` fired on a 2s timeout before long waits. Scope is
  limited to the shore-bank mesh helper, its runtime receipt, and ledgers. Hypothesis: removing
  the cross-shore cap while preserving the side-following bank strips will reduce the staged
  stripe read without adding overlays, changing water color ratios, or touching hard gates. Kill
  criteria: reject if structural scene gates fail, if water ROI regresses, if the lake/river edge
  becomes harsher or more empty than the accepted baseline, or if the stills still show the same
  full-width straight shore band.
- 2026-07-06: Loop 4 iteration 26 revised after dirty probe
  `phase4_open_shore_bank_dirty_probe_20260706`. The probe passed all real automated gates and
  failed only expected dirty-tree policy, but manual inspection and entity dumps falsified the
  near-cap hypothesis: selected logs had `shore_segments=0` and no current shore-bank receipt,
  while the visible strips lined up with `GenerativeExterior_CinematicTriplanarLayer0..7`.
  Reverted the inactive cap edit. Revised scope stays narrow: set compiler
  `triplanar_detail_layer_count` to zero and add a stale-IR runtime clamp/receipt for cinematic
  triplanar ground overlays only. This targets the active strip source without changing hard gates,
  water color ratios, or adding replacement overlay geometry.
- 2026-07-06: Loop 4 iteration 26 revised again after the triplanar dirty probe. The triplanar
  overlays were successfully removed (`triplanar_layers=0` and no current
  `GenerativeExterior_CinematicTriplanarLayer*` entities), but desert still had one full-width
  straight band. Entity dumps identified `GenerativeExterior_AuthoredRiverCutShadow`, a wide
  cube placed across the canyon river edge. Revised scope now also demotes that authored river-cut
  band with a runtime receipt, leaving the integrated terrain-water mesh and foreground ledge to
  own the bank. Kill criteria remain visual: reject if the desert band persists, if the canyon
  water/shore edge becomes harsher, or if any structural/semantic gate fails.
- 2026-07-06: Loop 4 iteration 26 accepted as a bounded strip-source cleanup, not a quality
  solve. Full runner `tools/run_genscene_acceptance.ps1 -Tag
  phase4_strip_source_demote_20260706` passed clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy, and rewrote
  `CURRENT.md` to accepted tag `phase4_strip_source_demote_20260706` at HEAD `4b141e0`.
  Heartbeat `cortex-aaa-accept-strip` fired on timeout twice while the runner built/rendered, then
  on process exit after the final structural gate. Accepted selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: the active strip
  emitters are demoted, but the stills remain flat, staged, and below AA/AAA. Next loop must be a
  larger visual-system pass: coherent terrain/backdrop massing, real hero asset/material fidelity,
  or renderer lighting/shadow integration that visibly changes the frame. Do not continue with
  small receipt-only strip cleanup unless an entity dump proves a specific visible artifact source.
- 2026-07-06: Loop 4 iteration 27 started as a structural composition and scene-depth slice.
  Orientation trusted `CURRENT.md` at accepted tag `phase4_strip_source_demote_20260706`; git
  status was clean on `main`. Heartbeat `cortex-aaa-loop27-proof` fired on a 2s timeout before
  long waits. Scope is limited to `src/Core/Engine_Scenes.cpp`, `tools/scene_compiler.py`, and
  ledgers. Hypothesis: adding an explicit composition spine to the generated exterior path
  (foreground anchors, grounded hero/midground placement, background massing, and renderer-owned
  depth/lighting receipts) will reduce the disconnected toy-stage read more than another
  artifact-demotion loop. Kill criteria: reject if any acceptance gate fails beyond expected
  dirty-tree policy, if prompt water-color semantics regress, if source/fallback receipts lose
  naturalistic dominance, if the change adds overlay/card/proof geometry or new `missing_*`
  gates, or if manual inspection says the selected stills remain dominated by flat staged
  cards, toy hero silhouettes, or disconnected props.
- 2026-07-06: Loop 4 iteration 27 rejected. Dirty probe 2 passed the non-visual checks except
  the campsite selected frame failed `purple_water_roi_fail`; manual inspection of
  `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0.png` also hit the slice kill criteria.
  The frame still read as a flat staged campsite with a toy tent, broad colored water sheet,
  card-like mountain/backdrop massing, and disconnected scattered props. A camera/ROI repair
  would only make a weak composition-spine slice green, so the production diff in
  `src/Core/Engine_Scenes.cpp`, `tools/scene_compiler.py`, and dirty `CURRENT_FAILED.md` was
  reverted. Do not retry Loop 27 as another framing/ROI pass; the next loop must replace visible
  fidelity sources directly: hero asset/material form, continuous terrain/backdrop massing, and
  renderer-owned lighting/shadow response that materially changes the selected frames.
- 2026-07-06: Loop 4 iteration 28 started as a hero asset/material form slice. Orientation
  trusted `CURRENT.md` at accepted tag `phase4_strip_source_demote_20260706`; git status was
  clean after rejection commit `a33e3bd`. Heartbeat `cortex-aaa-loop28-proof` fired on a 2s
  timeout before long waits. Scope is limited to generated hero mesh/material code,
  `tools/scene_compiler.py`, and ledgers. Hypothesis: replacing the visible campsite focal tent
  from primitive panel overbuild into one richer procedural canvas mesh with real material
  variation, seam/pole/rope geometry, and fewer slab-like panels will improve the dominant
  silhouette more than another composition/camera pass. Kill criteria: reject if semantic or
  structural gates fail, if water ROI/color semantics regress, if new hard pixel gates or
  overlay/proof geometry are added, if old panel clutter still dominates the tent, or if manual
  inspection says the selected campsite still reads as the same toy wedge with scattered props.
- 2026-07-06: Loop 4 iteration 28 dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_canvas_form_dirty_probe_20260706 -SkipBuild`
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates
  after a manual Release build. It failed only expected dirty-tree policy. Runtime receipts show
  `hero canvas mesh form shell=subdivided_canvas length_segments=22 slope_segments=8
  panel_overlays=0 fabric_layers=5 structural_poles=8 rope_stakes=6` and
  `hero asset replacement canvas_shell=1 canvas_panel_overlays=0`. Selected candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`; all passed quality and graphics gates with
  structural terrain/water receipts. Manual visual truth remains negative: the tent is less
  slab-stacked, but the frames still have flat water/backdrop massing, weak terrain material, and
  a visibly procedural hero silhouette. Accept only as a bounded hero canvas-form checkpoint;
  next loop must target a larger scene-system failure, not another tent-panel tweak.
- 2026-07-06: Loop 4 iteration 29 started as generated DXR/renderer lighting integration.
  Orientation trusted `CURRENT.md` at accepted tag `phase4_hero_canvas_form_20260706`; git
  status was clean on `main` after acceptance commit `1c53ac5`. Heartbeat
  `cortex-aaa-loop29-proof` fired on a 2s timeout before long waits. Scope is limited to
  generated render intent/plumbing (`tools/render_ir.ps1`, `tools/scene_compiler.py`,
  possibly `tools/scene_gen.py`) and ledgers. Hypothesis: generated high-fidelity stills should
  opt into the engine's existing DXR path from IR intent instead of requiring a manual
  environment variable, so renderer-owned reflections/shadowing can start replacing flat
  card-like lighting. Verifier is PARTIAL-RISK: acceptance runner plus logs must show the IR has
  `dxr_required=true` and render harness no longer forces `RT=off` for generated DXR stills;
  selected canonical images still require manual visual inspection and visual truth cannot be
  claimed AAA. Kill criteria: reject if DXR crashes/timeouts on the 3070 Ti path, if acceptance
  gates fail beyond expected dirty-tree policy, if the log still shows generated RT disabled for
  dxr-required IR, if `scene_graphics_gate.py` or water color ratios are touched, or if manual
  inspection shows no material lighting improvement and only receipt text changed.
- 2026-07-06: Loop 4 iteration 29 rejected after dirty probe
  `phase4_generated_dxr_dirty_probe_20260706 -SkipBuild`. The IR/harness proof succeeded:
  generated campsite IR had `dxr_required=true`, `render_ir.ps1` logged
  `generated_dxr_required=True`, runtime receipts logged `dxr_required=1`, and the previous
  `Renderer: env disables active ... RT=off` line was absent. The slice still hit kill criteria:
  structural scene gate failed, campsite failed `purple_water_roi_fail`, and alpine generated
  black 5 KB PNGs with `render_health_image` failure plus a DX12 queue fence timeout
  (`expected=19, completed=18`) and in-flight resource final-release validation error. Manual
  inspection also showed no coherent visual improvement: campsite remained a toy tent in front
  of a flat purple sheet and card-like mountains. Production changes in `tools/render_ir.ps1`,
  `tools/scene_compiler.py`, `tools/scene_gen.py`, and dirty `CURRENT_FAILED.md` were reverted.
  Do not retry broad generated DXR opt-in until the RT capture path is isolated with a small
  fixture that proves nonblack repeated captures under RT; the next active loop should target
  a non-RT structural quality source such as real water/shore subsystem routing, terrain/backdrop
  massing cleanup, or photoreal asset replacement.
- 2026-07-06: Loop 4 iteration 30 started as non-RT water/shore mesh structure. Orientation
  trusted `CURRENT.md` at accepted tag `phase4_hero_canvas_form_20260706`; git status was clean
  after rejection commit `3ae4bd9`. Heartbeat `cortex-aaa-loop30-proof` fired on a 2s timeout.
  Scope is limited to generated water/shore mesh geometry and compiler IR budgets, not gates.
  Hypothesis: the visible flat lake/river sheet can be improved by making the generated water
  boundary and integrated terrain-water transition curved, irregular, and locally banked in the
  structural mesh itself, with `WaterSurfaceComponent` still owning rendering. This must avoid
  overlay/card/proof geometry, water color-ratio tuning, `scene_graphics_gate.py`, and DXR.
  Verifier is PARTIAL-RISK: python compile, Release build, dirty acceptance probe, runtime
  receipts for the structural mesh, and manual inspection of the three canonical stills. Kill
  criteria: reject if semantic/graphics/structural gates fail beyond expected dirty-tree policy,
  if prompt water color ROI regresses, if straight/luminous water sheets still dominate, if the
  change creates visible strips/cards, or if the scenes become emptier/flatter.
- 2026-07-06: Loop 4 iteration 30 dirty probe
  `phase4_banked_water_mesh_dirty_probe_20260706 -SkipBuild` passed ratchet freeze, Python
  compile, curation, graphics reset, and structural scene gates after a manual Release build;
  it failed only expected dirty-tree policy. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`, all quality/graphics green. Runtime receipts
  prove `shore_profile=asymmetric_banked` with `banked_curved_lake_cove` / `banked_s_curve_river`
  and higher water mesh tessellation. Manual visual truth remains negative: shoreline silhouettes
  are less rectangular, but the water still reads as a flat luminous sheet and the scenes remain
  staged/toy-like. Accept only as a bounded structural water-mesh checkpoint; the next loop must
  attack a larger non-water fidelity source such as terrain/backdrop massing or photoreal asset
  replacement.
- 2026-07-06: Loop 4 iteration 30 accepted as a bounded structural water/shore mesh checkpoint.
  Full runner `tools/run_genscene_acceptance.ps1 -Tag phase4_banked_water_mesh_20260706` passed
  clean tree, ratchet freeze, Python compile, curation, graphics reset, Release build,
  structural scene gate, and phase0 policy, and rewrote `CURRENT.md` to accepted tag
  `phase4_banked_water_mesh_20260706` at HEAD `c77a42b`. Selected candidates were campsite
  `build/bin/logs/gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `build/bin/logs/gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `build/bin/logs/gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative:
  the water/shore silhouettes are less rectangular, but the frame still reads as flat,
  disconnected, and below AA/AAA. Next active loop must attack a larger fidelity source:
  terrain/backdrop massing, photoreal asset replacement, or renderer/material integration with a
  fixture first. Do not continue with water color tuning, metric gates, or overlay/proof geometry.
- 2026-07-06: Loop 4 iteration 31 started as far-shore terrain/backdrop massing. Orientation
  trusted `CURRENT.md` at accepted tag `phase4_banked_water_mesh_20260706`; git status was clean
  after checkpoint commit `4db79f6`. Heartbeat `cortex-aaa-loop31-proof` fired on a 2s timeout.
  Scope is limited to structural generated terrain/backdrop geometry in `src/Core/Engine_Scenes.cpp`
  plus ledgers. Hypothesis: adding a broad opaque far-shore/foothill apron that uses the same
  shared terrain noise and material system will make lakes/rivers read as coherent outdoor spaces
  instead of flat water sheets in front of detached ridge cards. Verifier is PARTIAL-RISK:
  Release build, dirty acceptance probe, runtime receipts for the new far-shore mass mesh, and
  manual inspection of the three canonical stills. Kill criteria: reject if any real acceptance
  gate fails beyond expected dirty-tree policy, if prompt water-color semantics regress, if the
  new mass occludes the hero/water ROI, if it reads as another card/strip/overlay, or if the
  selected frames remain visually dominated by disconnected backdrop sheets.
- 2026-07-06: Loop 4 iteration 31 rejected after two dirty probes. Probe 1
  `phase4_far_shore_apron_dirty_probe_20260706 -SkipBuild` and probe 2
  `phase4_far_shore_apron_dirty_probe2_20260706 -SkipBuild` both passed ratchet freeze, Python
  compile, curation, graphics reset, and structural scene gates after a manual Release build,
  and failed only expected dirty-tree policy. Receipts proved the attempted systems:
  `far-shore terrain mass apron=opaque_shared_fbm water_cutout=profile_following` and
  `profile=volumetric_foothill_facets rows=7`. Selected probe-2 images were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Manual visual truth failed the loop: the far
  shore and ridges were somewhat less wafer-thin, but the stills remained dominated by flat
  luminous water, hard detached mountain silhouettes, toy campsite/cabin staging, and weak
  material/light response; all selected probe-2 scene-gen verdicts remained `reframe`. Production
  changes in `src/Core/Engine_Scenes.cpp` and dirty `CURRENT_FAILED.md` were reverted. Do not retry
  terrain massing as more procedural ridge/apron geometry until asset/material and renderer
  fidelity fronts are stronger.
- 2026-07-06: Loop 4 iteration 32 started as a source/photoreal foreground ownership slice.
  Orientation trusted `CURRENT.md` at accepted tag `phase4_banked_water_mesh_20260706`; git status
  was clean after rejection commit `1cbf0db`. Heartbeat `cortex-aaa-loop32-proof` fired on a 2s
  timeout before long waits. Scope is limited to source asset texture binding, compiler budgets,
  authored campsite source-cluster placement/materials, and ledgers. Hypothesis: the scanned
  naturalistic assets already in the repo must visibly own the campsite foreground before more
  procedural terrain/ridge work can help; enlarge/reposition real logs, branches, boulders,
  table, and lanterns while demoting the primitive tent shadow backing and tightening the
  campsite-only camera around the source foreground cluster. Verifier is
  PARTIAL-RISK: Release build, dirty acceptance probe, receipts for
  `foreground_owned=1` and expanded source hero counts, plus manual inspection of the canonical
  campsite/alpine/desert stills. Kill criteria: reject if acceptance gates fail beyond dirty-tree
  policy, if water color semantics regress, if source assets become clutter instead of grounding
  the scene, if new hard pixel gates/overlays/proof geometry are added, or if manual inspection
  says the selected stills remain in the same disconnected toy-stage failure class.
- 2026-07-06: Loop 4 iteration 32 rejected. The dirty probe
  `phase4_source_hero_asset_cluster_dirty_probe_20260706 -SkipBuild` passed ratchet freeze,
  Python compile, curation, graphics reset, and skipped Release build after a manual green build,
  but failed structural scene gate because the first source-budget edit leaked into the desert
  canyon campsite prompt and all three desert candidates failed `turquoise_water_roi_fail`.
  A targeted desert rerun after tightening the compiler condition produced a hard-valid
  iteration again, proving the leak. Manual campsite inspection still rejected the slice:
  adding `WoodenChair_01` made the scene visibly worse with indoor/throne-like furniture, and
  after removing chairs the frame remained an oversized flat purple lake with a toy tent and
  disconnected tiny props. A campsite-only camera tightening then failed `purple_water_roi_fail`
  in all three iterations and still did not escape the visual failure class. Production changes
  in `src/Core/Engine_Scenes.cpp`, `tools/scene_compiler.py`, and dirty `CURRENT_FAILED.md` were
  reverted. Do not retry this as larger local prop clutter or camera-only framing; the next loop
  must change a true renderer/material/asset source, such as an isolated RT/AO fixture or a real
  curated exterior asset module, before more composition tuning.
- 2026-07-06: Loop 4 iteration 33 started as an isolated renderer RT/AO ground-truth fixture.
  Orientation trusted `CURRENT.md` at accepted tag `phase4_banked_water_mesh_20260706`; git status
  was clean on `main` after rejection commit `6c50693`. Heartbeat `cortex-aaa-loop33-proof` fired
  on a 2s timeout before long waits. Scope is limited to a narrow fixture/verifier wrapper around
  existing curated `rt_showcase` diagnostics and ledgers; no generated-scene production changes
  are allowed in this loop. Hypothesis: broad generated DXR failed because the integration target
  was too wide, so the next safe step is to prove the renderer path itself with repeated nonblack
  captures, RT enabled, TLAS instances present, SSAO/occlusion evidence present, and isolated
  artifacts. Verifier is a fixture-building loop: first prove the new fixture can fail on a bad
  threshold/missing evidence, then prove it green on two `rt_showcase` runs. Kill criteria: reject
  if RT captures are black/flaky, if frame-contract evidence is missing, if the fixture requires
  weakening old generated-scene gates, if it edits `scene_graphics_gate.py`, or if it becomes a
  backdoor broad generated DXR opt-in.
- 2026-07-06: Loop 4 iteration 33 verifier red/green proof completed. The first red-control run
  rendered `rt_showcase` cleanly but the fixture initially threw while serializing its summary;
  that fixture bug was fixed and rerun. Proven red command:
  `tools/run_rt_nonblack_fixture.ps1 -NoBuild -IsolatedLogs -Runs 1 -SmokeFrames 80
  -MaxExpectedFrames 120 -MinVisualNonBlackRatio 1.01`; exit was nonzero for the intended gate
  failure, `run 1 nonblack_ratio=1.0, expected >= 1.01`, with summary
  `build/bin/logs/runs/rt_nonblack_fixture_20260706_212915_603_260472_043ddaad/rt_nonblack_fixture_summary.json`.
  Proven green command:
  `tools/run_rt_nonblack_fixture.ps1 -NoBuild -IsolatedLogs -Runs 2 -SmokeFrames 120
  -MaxExpectedFrames 160 -MinTLASInstances 8 -MinRayTracingPasses 3 -MinVisualNonBlackRatio 0.95
  -MinVisualAvgLuma 20 -MinVisualCenterLuma 20`; exit 0, summary
  `build/bin/logs/runs/rt_nonblack_fixture_20260706_213018_814_164144_0f693965/rt_nonblack_fixture_summary.json`.
  Both green runs reported `RT=true`, `SSAO=true`, `tlas=63`, `rt_passes=9`, `nonblack=1.000`,
  and luma/center-luma around `117/137`. This proves the isolated renderer path, not generated
  scene visual quality; visual truth for GenScene remains negative until the renderer/material
  evidence is reconnected through a separate accepted loop.
