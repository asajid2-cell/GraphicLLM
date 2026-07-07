# GenScene Queue

`CURRENT.md` wins for state. This file records queue items and accept/reject outcomes.

## Active Queue

1. Phase 4 authored lighting/material/assets/composition: reduce flat-sheet/patch debris and
   improve art direction without adding pixel-proxy gates.

## Accept / Reject Log

- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase0_dirty_overlay_audit_b_20260706`
  wrote `CURRENT_FAILED.md`. Result: RED. `python_compile` and `release_build` passed, but
  `clean_tree`, `gate_ratchet_freeze`, and `phase0_policy` failed. The dirty overlay diff is not
  accepted state.
- 2026-07-06: Claude review adopted as strategic correction. Old overlay loop is no longer the
  active route. `aaa-loop43-codex` heartbeat retired to prevent zombie continuation.
- 2026-07-06: heartbeat proof for the new campaign label succeeded:
  `node z:/328/CMPUT328-A2/codexworks/301/heartbeat/bin/hb.mjs wait --label genscene-aaa-coherent --timeout 1 --poll 1`
  fired by timeout after 1s.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase0_dirty_overlay_audit_c_20260706`
  wrote `CURRENT_FAILED.md`. Result: RED for the same intended reasons: dirty tree,
  added `missing_*` ratchet lines, and phase0 dirty policy. This is the rejection baseline.
- 2026-07-06: rejected the old Loop 43 production/overlay diff and saved a copy under
  ignored `artifacts/genscene_acceptance/rejected_loop43_overlay_20260706/loop43_rejected.diff`.
  Restored shader, engine, compiler, render script, scene generator, and graphics gate files to
  `HEAD`; only campaign-state/docs hygiene remains dirty for checkpointing.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase0_clean_freeze_20260706`
  wrote green `CURRENT.md`. Result: PASS for `clean_tree`, `gate_ratchet_freeze`,
  `python_compile`, `release_build`, and `phase0_policy`. Loop 0 freeze/checkpoint is accepted;
  next active item is Phase 1 curation.
- 2026-07-06: Loop 1 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase1_curation_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. `curation_gate`, `python_compile`, and
  `gate_ratchet_freeze` passed; `clean_tree` and `phase0_policy` failed because the curation
  implementation was intentionally uncommitted.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase1_curation_20260706`
  wrote green `CURRENT.md`. Result: PASS for `clean_tree`, `gate_ratchet_freeze`,
  `python_compile` including `tools/curate_gallery.py`, `curation_gate`, `release_build`,
  and `phase0_policy`. Loop 1 curation is accepted; next active item is Phase 2 gate reset.
- 2026-07-06: Loop 2 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase2_gate_reset_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. `graphics_gate_reset`, `curation_gate`,
  `python_compile`, and `gate_ratchet_freeze` passed; `clean_tree` and `phase0_policy`
  failed because the gate reset implementation was intentionally uncommitted.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase2_gate_reset_20260706`
  wrote green `CURRENT.md`. Result: PASS for `clean_tree`, ratchet freeze, Python compile,
  curation, `graphics_gate_reset`, Release build, and phase0 policy. Loop 2 gate reset is
  accepted; next active item is continuous terrain/water.
- 2026-07-06: Loop 3 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase3_structural_terrain_water_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. `structural_scene_gate`, graphics reset,
  curation, Python compile, and ratchet freeze passed; `clean_tree` and `phase0_policy`
  failed because the structural terrain/water implementation was intentionally uncommitted.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase3_structural_terrain_water_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, structural scene gate, Release build, and phase0 policy.
  Loop 3 structural terrain/water is accepted. Visual residual remains negative: generated
  stills are semantically cleaner and structurally instrumented, but still show broad water
  sheets, patch debris, and kit silhouettes.
- 2026-07-06: Loop 4 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_overlay_subtraction_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. The subtraction diff passed ratchet freeze,
  Python compile, curation, graphics reset, and structural scene gates; it failed only
  `clean_tree` and `phase0_policy` because the implementation was intentionally uncommitted.
  Direct canonical renders also passed semantic and graphics gates:
  `gen_a_foggy_mountain_campsite_beside_2.png`,
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and
  `gen_a_sunny_desert_canyon_campsite_w_2.png`. Visual truth remains negative; this is
  accepted only as debris subtraction, not as AAA quality.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_overlay_subtraction_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, structural scene gate, Release build, and phase0 policy.
  Loop 4 remains open; next work must replace flat lighting/material/composition systems,
  not add proxy overlays.
- 2026-07-06: Loop 4 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_water_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. The shaped-water diff passed ratchet freeze,
  Python compile, curation, graphics reset, and structural scene gates; it failed only
  `clean_tree` and `phase0_policy` because the implementation was intentionally uncommitted.
  Runtime receipts now report `shape=curved_lake_cove` for lake prompts and
  `shape=s_curve_river` for the desert river. Canonical semantic/graphics gates passed after
  adjustment; desert turquoise ROI recovered to `turquoise_fraction=0.4073`. Visual residuals
  remain: lakes are still broad/flat, foreground dressing is still noisy, and assets remain
  stylized.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_water_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, structural scene gate, Release build, and phase0 policy.
  Loop 4 remains open; next item is foreground/material/asset coherence.
- 2026-07-06: Loop 4 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_composition_cleanup_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. The composition cleanup passed ratchet freeze,
  Python compile, curation, graphics reset, and structural scene gates; it failed only
  dirty-tree policy gates. The change removes forced minimum foreground/water strip counts,
  zeros foreground dressing clusters and micro-detail overlays, and keeps canonical semantic
  plus graphics gates green. Visual residuals remain: hero campsite detail is still busy,
  water/lake materials remain too flat, and source assets are still below the target fidelity.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_composition_cleanup_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, structural scene gate, Release build, and phase0 policy.
  Loop 4 remains open; next item is hero mesh/material replacement and material response.
- 2026-07-06: Loop 4 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_material_cleanup_dirty_probe2_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md`. Result: `structural_scene_gate` failed because the runner
  was still hardcoded to stale desert candidate `_2` while the generator selected a valid
  best candidate. This was treated as a verifier wiring defect, not as visual acceptance.
- 2026-07-06: Loop 4 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_material_cleanup_dirty_probe3_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. The runner now renders each canonical prompt
  fresh, parses `best render:`, and gates the selected candidate. It passed ratchet freeze,
  Python compile, curation, graphics reset, and structural scene gates; it failed only
  `clean_tree` and `phase0_policy` because the implementation was intentionally uncommitted.
  Visual truth remains negative; this checkpoint is only a hero/material overpaint cleanup
  and runner freshness fix.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_material_cleanup_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, fresh structural scene generation/gating, Release build, and
  phase0 policy. Loop 4 remains open; next active item is a real lighting/material/asset
  system pass, not more overlay-count trimming.
- 2026-07-06: Loop 4 dirty verifier probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_asset_ladder_dirty_probe6_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. The source-asset ladder diff passed ratchet
  freeze, Python compile, curation, graphics reset, and structural scene gates, and failed
  only dirty-tree policy gates. The runner now builds before structural renders on full
  runs, and `scene_gen.py` ranks candidates by the hard semantic gate before critic score.
  Visual truth remains negative: the scenes are cleaner and less primitive-heavy, but still
  not a serious AAA result.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_source_asset_ladder_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, build-before-render Release build, fresh structural scene gates,
  and phase0 policy. Loop 4 remains open; next active item is lighting/water/material
  coherence and real shadowing, because the current accepted stills are still visibly below
  AA/AAA quality.
- 2026-07-06: Loop 4 renderer-owned shadow/water cleanup dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_shadow_water_dirty_probe_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile,
  curation, graphics reset, Release build, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy` because the implementation was intentionally uncommitted.
  The change removes several Goodhart-style proxy overlay requests: contact patches, soft
  penumbra disks, image contact occluders, translucent shadow bands, water glint/ripple strips,
  and authored water cards. Visual inspection remains negative: scenes are cleaner but still
  flat, low-poly, and not AA/AAA. Accept only as a structural cleanup checkpoint, then continue
  to asset/composition/real material fronts.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_shadow_water_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is a real asset/composition/material-form pass: low-poly tent,
  cabin, tree, rock, and canyon silhouettes still dominate, and lakes still read as broad sheets
  despite the renderer-owned cleanup.
- 2026-07-06: Loop 4 hero asset/composition dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_asset_form_dirty_probe_20260706`
  wrote red `CURRENT_FAILED.md` with a real `structural_scene_gate` failure: desert selected
  render failed `turquoise_water_roi_fail` after the river geometry was narrowed. Do not retry
  that exact water narrowing without also preserving prompt color readability.
- 2026-07-06: Loop 4 hero asset/composition dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_asset_form_dirty_probe2_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile, curation,
  graphics reset, Release build, and structural scene gates, and failed only dirty-tree policy
  gates. The change removes several primitive clutter sources and demotes stylized catalog
  heroes, but visual truth remains negative. Accept only as cleanup; next active item must attack
  the remaining real visual ceiling: primitive tent/cabin forms, flat water/shore integration,
  and backdrop/material lighting depth.
- 2026-07-06: Loop 4 grounded composition/material dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_grounded_composition_material_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected after a manual VsDevCmd-backed Release build. It
  passed ratchet freeze, Python compile, curation, graphics reset, and structural scene gates,
  and failed only dirty-tree policy gates. The change is a limited real visual pass: curved
  shore-bank geometry instead of full-width shore strips, a subdivided/open-front canvas tent,
  hero-grounded campsite/desert camera defaults, lower water emissive/alpha, dry red-brown dirt
  material response, and brighter canyon ambient/exposure. Visual truth remains negative: accept
  only as a bounded geometry/material/composition checkpoint; next active work must attack the
  still-flat water surface, cardboard mountain/backdrop forms, and weak asset library ceiling.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_grounded_composition_material_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is not more camera/material cleanup; target the remaining
  structural visual ceiling: water shader/surface depth, mountain/backdrop geometry, and real
  non-Kenney asset replacement.
- 2026-07-06: Loop 4 water/ridge depth dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_water_ridge_depth_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected after manual Release builds. It passed ratchet freeze,
  Python compile, curation, graphics reset, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy` because the implementation was intentionally uncommitted.
  The change moves water/backdrop work into real systems: cove/tapered generated water meshes,
  shore-to-far water shader depth, lower authored water glow, and faceted ridge-mass geometry.
  A real desert `turquoise_water_roi_fail` appeared during focused renders and was fixed with a
  river-only material correction before the probe. Visual truth remains negative: cleaner, but
  still staged/disconnected and below AA/AAA. Accept only as a bounded water/backdrop systems
  checkpoint, then continue to asset replacement, terrain composition, and stronger lighting.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_water_ridge_depth_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is real asset/composition/lighting coherence: the water and
  ridge systems are less proxy-like, but the accepted stills still look staged and disconnected.
- 2026-07-06: Loop 4 source cluster dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_source_cluster_dirty_probe_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected after a manual Release build. It passed ratchet
  freeze, Python compile, curation, graphics reset, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy` because the implementation was intentionally uncommitted.
  The change replaces more authored campsite primitives with loaded source meshes and irregular
  terrain patch grounding. Visual truth remains negative; accept only as a bounded source-cluster
  checkpoint, then continue cleaning debris/shore artifacts and replacing hero forms.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_source_cluster_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is artifact cleanup and stronger coherent composition: the
  campsite cluster is less primitive, but accepted stills still have strip artifacts and clutter.
- 2026-07-06: Loop 4 shore-strip/readability dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shore_strip_cleanup_dirty_probe3_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected after manual Release builds. It passed ratchet
  freeze, Python compile, curation, graphics reset, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy`. The change removes old authored shore slabs when curved
  shore layers exist, moves canyon rivers closer as actual water, strengthens canyon river water
  material read, and darkens/shortens canyon wet-shore patches. Visual truth remains negative:
  accept only as a bounded artifact/readability checkpoint, then continue with debris removal,
  coherent hero/background replacement, and stronger lighting/material form.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_shore_strip_cleanup_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is still real visual coherence: remove remaining strip/clutter
  artifacts and replace weak hero/background forms with higher-fidelity geometry/materials.
- 2026-07-06: Loop 4 visible-card cleanup dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_visible_card_cleanup_dirty_probe2_20260706 -SkipBuild`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile, curation,
  graphics reset, and structural scene gates, and failed only `clean_tree` plus `phase0_policy`
  because the implementation was intentionally uncommitted. The change zeros visible hero
  material/shadow/volumetric/wet-roughness card requests and fixes a real campsite
  `purple_water_roi_fail` by strengthening purple/violet water material. Visual truth remains
  negative; accept only as a bounded visible-card cleanup checkpoint, then continue into the
  suspected bloom/emissive/practical-light ghosting and weak hero/background forms.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_visible_card_cleanup_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is suspected bloom/emissive/practical-light ghosting and
  weak hero/background form, because visible-card compiler budgets are now zero but the latest
  stills are not visually coherent AA/AAA shots.
- 2026-07-06: Loop 4 emissive/bloom cleanup dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_emissive_bloom_cleanup_dirty_probe2_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile, curation,
  graphics reset, Release build, and structural scene gates, and failed only `clean_tree` plus
  `phase0_policy` because the implementation was intentionally uncommitted. The change reduces
  generated exterior bloom smearing, adds per-material emissive bloom control, removes the cabin
  warm-light spill slab, and shrinks the camp/cabin visible glow geometry. Visual truth remains
  negative; accept only as an artifact cleanup checkpoint, then continue with water/shore band
  removal, backdrop massing, and higher-fidelity hero/background forms.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_emissive_bloom_cleanup_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Next active item is water/shore band geometry plus stronger backdrop and hero
  forms; the accepted stills are cleaner around emissives but are still not coherent AA/AAA shots.
- 2026-07-06: Loop 4 water/visible-noise dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_visual_noise_cleanup_dirty_probe_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile,
  curation, graphics reset, Release build, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy` because the implementation was intentionally uncommitted.
  The change disables remaining visible haze/environment strip emitters, moves shore props to
  flank grounding, reduces recipe/global particle speckle, and demotes primitive/Kenney tree
  clutter. Selected candidates were campsite `gen_a_foggy_mountain_campsite_beside_0.png`,
  alpine `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: frames are
  cleaner, but water sheets, primitive tent/cabin forms, and weak backdrop/asset quality still
  block a real AA/AAA result.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_visual_noise_cleanup_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Next active item is a real hero-form and
  water-surface pass, because the accepted images are cleaner but still not visually coherent
  AA/AAA scenes.
- 2026-07-06: Loop 4 hero/water form dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_water_form_dirty_probe_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile,
  curation, graphics reset, Release build, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy`. The change removes the catalog `tent_smallClosed` wedge
  from generated campsites so the procedural canvas tent owns the silhouette, increases only
  procedural canvas seam/pole/rope detail, and gives the generated water mesh a non-straight
  shore edge plus subtle surface/material variation. Selected candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_0.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Visual truth remains negative: hero form and
  water are incrementally cleaner, but material fidelity, water shading depth, and backdrop/asset
  quality are still below the target.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_hero_water_form_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open; this is accepted only as a bounded hero-form/water-geometry checkpoint. Next
  active item is a real lighting/material/backdrop pass, because the accepted stills still lack
  convincing material depth, water shading, and high-quality scene cohesion.
- 2026-07-06: Loop 4 shaped waterbed dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_waterbed_dirty_probe2_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile,
  curation, graphics reset, Release build, and structural scene gates, and failed only
  `clean_tree` plus `phase0_policy`. The change replaces the full-width tinted seabed rectangle
  with a neutral far-shore floor plus a shaped underwater bed that shares the generated
  lake/river footprint, and tightens generated water-body width/resolution. Selected candidates
  were campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_2.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: water bodies are
  better bounded and less like full-frame color sheets, but the front edge, backdrop massing,
  lighting, and hero assets are still below AA/AAA quality.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_shaped_waterbed_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open. Selected accepted candidates were campsite
  `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_1.png`. Next active item is backdrop/terrain/lighting
  cohesion: water footprints are cleaner, but the frames still read staged and low-fidelity.
- 2026-07-06: rejected Loop 4 foothill/backdrop dirty probe
  `phase4_foothill_backdrop_dirty_probe_20260706`. The runner passed ratchet freeze,
  Python compile, curation, graphics reset, Release build, and structural scene gates, but
  visual inspection showed the new foothill aprons as broad pale shelf/slab bands in the
  desert path and no meaningful quality lift in campsite/alpine. This was a Goodhart-risk
  geometry accretion, so the production diff and dirty `CURRENT_FAILED.md` state were reverted.
  Next active item remains real terrain/backdrop/lighting cohesion, preferably by replacing
  slab/backdrop cheats with continuous terrain and material/lighting response.
- 2026-07-06: Loop 4 declutter/composition dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_declutter_camera_dirty_probe2_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile,
  curation, graphics reset, Release build, and structural scene gates, failing only
  `clean_tree` plus `phase0_policy`. The change zeros visible terrain/material patch emitters,
  disables backdrop detail ridge accretion, reduces random ecology/source-clutter budgets,
  narrows generated water footprints, strengthens purple water material, and switches the
  canonical exteriors to tighter module-specific camera profiles. Selected candidates were
  campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Visual truth remains negative: shots are
  cleaner and more coherent, but cardboard backdrops, flat water/shore reads, and simple hero
  geometry still block the AAA goal.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_declutter_camera_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy.
  Accepted candidates were campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Loop 4 remains open; next active item should
  attack remaining flat water/shore bands, cardboard backdrop masses, and hero-asset material
  fidelity rather than declaring this a quality solve.
- 2026-07-06: Loop 4 backdrop material-depth dirty probe
  `tools/run_genscene_acceptance.ps1 -Tag phase4_backdrop_material_depth_dirty_probe2_20260706`
  wrote red `CURRENT_FAILED.md` as expected. It passed ratchet freeze, Python compile,
  curation, graphics reset, Release build, and structural scene gates, failing only
  `clean_tree` plus `phase0_policy`. The first attempt in this lane was visually rejected
  because object-scale cliff textures made the desert backdrop too dark. The adjusted change
  keeps existing ridge geometry but routes it through terrain-scale material sources and caps
  distant backdrop response; runtime logs show `backdrop material depth textured_surfaces=2
  ridge_layers=2` and `backdrop_surfaces=8`. Visual truth remains negative: this is a small
  material-depth checkpoint, not the AAA solve. Next active item should move to real water/shore
  shading or hero asset material fidelity, not another ridge/card layer.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_backdrop_material_depth_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy.
  Accepted candidates were campsite `gen_a_foggy_mountain_campsite_beside_1.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_1.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Loop 4 remains open; next active item is real
  water/shore shading or hero asset/material fidelity. Do not continue ridge/card tuning unless
  a later front proves backdrop massing is the blocker again.
- 2026-07-06: rejected Loop 4 water transmission/depth dirty probes
  `phase4_water_transmission_depth_dirty_probe_20260706` through
  `phase4_water_transmission_depth_dirty_probe6_20260706`. The slice stayed trapped in flat
  colored-water-sheet visuals and then oscillated between `purple_water_roi_fail` and
  `turquoise_water_roi_fail`; accepting it would continue the metric-ratio rut. Reverted the
  production diff and dirty `CURRENT_FAILED.md`. Next active item is hero asset/material fidelity
  or real shore/terrain/composition structure, not more water-color tuning.
- 2026-07-06: rejected Loop 4 hero-cluster grounding dirty probe
  `phase4_hero_cluster_grounding_dirty_probe2_20260706`. The runner passed ratchet freeze,
  Python compile, curation, graphics reset, Release build, and structural scene gate, failing only
  expected dirty-tree policy gates, but selected campsite/desert stills stayed visibly
  disconnected and low-fidelity: toy tent/prop clusters in front of flat water and backdrop forms.
  This hit the slice kill criteria and is rejected to avoid continuing the incremental
  local-minimum pattern. Next active item is a higher-leverage system pass: continuous terrain,
  real water/shore integration, lighting/shadow/material response, or asset replacement. Do not
  spend the next slice on more clutter/camera/card tuning.
- 2026-07-06: Loop 4 renderer/material coherence dirty probe
  `phase4_renderer_material_coherence_dirty_probe_20260706 -SkipBuild` passed all real gates after
  a manual Release build and failed only expected dirty-tree policy. The slice removes the late
  source-readability washout by preserving stronger SSAO/shadow settings and lowering ambient/IBL
  lift; selected stills are darker with better contact but still far below AA/AAA. Accept only as
  a bounded renderer/material checkpoint. Next active item must be a larger structural front:
  continuous terrain/backdrop replacement, water/shore integration, or real photoreal asset
  replacement, not another tiny lighting tweak.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_renderer_material_coherence_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Selected
  candidates were campsite `gen_a_foggy_mountain_campsite_beside_2.png`, alpine
  `gen_a_stormy_alpine_lake_with_a_smal_0.png`, and desert
  `gen_a_sunny_desert_canyon_campsite_w_0.png`. Loop 4 remains open and visual truth remains
  negative. Next active item is a bigger structural quality front, not more renderer knob tuning.
- 2026-07-06: active item is Loop 4 iteration 23, source-asset dominance. Replace the
  source-environment lane's fetched/Kenney/procedural-looking backdrops with already-available
  naturalistic CC0 scan assets and runtime receipts proving naturalistic dominance. This is a
  structural asset-material slice, not a new metric or lighting knob pass.
- 2026-07-06: Loop 4 iteration 23 dirty probe passed all real gates and proved naturalistic
  source-environment dominance (`fallback_total=0`, `naturalistic_total=15-17`). Visual truth
  remains negative, so this can only be accepted as a bounded checkpoint. After clean acceptance,
  the next active item must attack the remaining structural image failures: flat water/shore,
  cardboard terrain/backdrops, and toy hero camp geometry.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_source_asset_dominance_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open and visual truth remains negative. Next active item is continuous terrain/water/
  hero-geometry structure, not more source-asset count work.
- 2026-07-06: active item is Loop 4 iteration 24, integrated terrain-water continuity. Replace
  the split flat generated exterior base with a continuous heightfield/shore/waterbed mesh and
  derive water normals from displaced water geometry, while demoting compiler-requested visible
  strip/guyline budgets that fight the base. This is a structural base-scene slice: no new hard
  pixel gates, no water color-ratio tuning, no overlay/card proof layers, and no fallback/Kenney
  source asset regression.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag phase4_integrated_terrain_water_20260706`
  wrote green `CURRENT.md`. Result: PASS for clean tree, ratchet freeze, Python compile,
  curation, graphics reset, Release build, structural scene gate, and phase0 policy. Loop 4
  remains open and visual truth remains negative. Next active item is a higher-leverage visual
  system pass: backdrop/terrain massing, hero asset fidelity, or renderer/material lighting depth,
  not more water color-ratio tuning or overlay/card proof geometry.
- 2026-07-06: active item is Loop 4 iteration 25, artifact-removal plus renderer-owned exterior
  quality. Demote remaining visible synthetic emitters that survived the terrain-water checkpoint
  and add runtime receipts for renderer-owned SSAO/shadow/fog/SSR/optional-DXR exterior depth.
  This is not a new metric pass: no new `missing_*` hard gates, no water color-ratio tuning, no
  new card/proof geometry, and no weakening of the acceptance runner.
- 2026-07-06: rejected Loop 4 iteration 25 after dirty probes
  `phase4_artifact_renderer_lighting_dirty_probe3_20260706` and
  `phase4_artifact_renderer_lighting_dirty_probe4_20260706`. The probes were structurally green
  but visually failed: campsite/alpine/desert remained flat, staged, and strip-dominated despite
  the emitter demotion receipts. Production source and dirty `CURRENT_FAILED.md` were reverted.
  Next active item is a high-leverage structural replacement slice: real terrain/backdrop
  composition, hero asset/material replacement, or renderer lighting that materially changes the
  selected frames. Do not spend the next iteration on receipt-only cleanup, water color ratios, or
  another overlay/card/strip emitter.
- 2026-07-06: active item is Loop 4 iteration 26, open-ended shore bank geometry. Remove the
  shore-bank near cap that bridges left and right banks across the foreground water edge, because
  it creates the visible long straight tan/peach shore strip. Preserve side-following bank
  geometry and structural terrain/water receipts; no hard-gate edits, no water color tuning, no
  new card/overlay emitters.
- 2026-07-06: Loop 4 iteration 26 revised: the shore-bank cap was not active in selected probe
  logs. Entity dumps show the foreground strips are `GenerativeExterior_CinematicTriplanarLayer*`.
  Active item is now targeted triplanar-overlay demotion: zero `triplanar_detail_layer_count` and
  clamp stale IR for that count only, then rerun the dirty probe and reject if the strips remain
  or the scenes become emptier/flatter.
- 2026-07-06: Loop 4 iteration 26 second revision: triplanar overlays were gone, but desert still
  showed a full-width authored river band. Demote `GenerativeExterior_AuthoredRiverCutShadow`
  and verify the selected desert still no longer has the straight foreground strip.
- 2026-07-06: `tools/run_genscene_acceptance.ps1 -Tag
  phase4_strip_source_demote_20260706` wrote green `CURRENT.md`. Result: PASS for clean tree,
  ratchet freeze, Python compile, curation, graphics reset, Release build, structural scene gate,
  and phase0 policy. Loop 4 remains open and visual truth remains negative. Next active item is a
  larger visual-system loop: replace staged terrain/backdrop/hero/light structure with coherent
  scene massing and material response. Avoid more tiny emitter demotions, water color tuning,
  metric gates, or overlay/card proof geometry.
- 2026-07-06: active item is Loop 4 iteration 27, structural composition and scene depth. Build
  a real foreground/midground/background composition path for generated exteriors using existing
  terrain, water, renderer, and naturalistic asset systems. Do not touch `scene_graphics_gate.py`,
  do not add new `missing_*` gates, do not tune water color ratios, and do not add overlay/card
  proof geometry.
- 2026-07-06: rejected Loop 4 iteration 27 after dirty probe 2 and manual inspection. The
  composition-spine diff still produced campsite imagery dominated by a toy tent, flat stage,
  broad purple water sheet, card-like backdrop massing, and disconnected props; it also failed
  the selected campsite `purple_water_roi_fail`. Reverted production source and dirty
  `CURRENT_FAILED.md`. Do not spend the next iteration on framing/ROI repair for this slice.
  Next active item is a higher-leverage fidelity replacement loop: real hero asset/material form,
  continuous terrain/backdrop massing, and renderer lighting/shadow behavior that visibly changes
  the image rather than satisfying a receipt.
- 2026-07-06: active item is Loop 4 iteration 28, hero asset/material form. Replace the
  campsite focal tent's slab/panel overbuild with richer procedural canvas mesh/material detail
  and remove panel clutter that keeps the selected frames toy-like. Do not touch
  `scene_graphics_gate.py`, do not add hard pixel gates, do not tune water color ratios, and do
  not add overlay/card proof geometry.
- 2026-07-06: Loop 4 iteration 28 dirty probe
  `phase4_hero_canvas_form_dirty_probe_20260706 -SkipBuild` passed all real gates and failed
  only expected dirty-tree policy after manual Release build. Receipts prove the campsite hero
  path now uses a subdivided canvas shell with `panel_overlays=0`, 8 structural poles, and 6
  rope stakes. Visual truth remains negative; accept only as a bounded hero-form checkpoint, then
  move to a larger scene-system failure such as terrain/backdrop massing, water/shore shading, or
  renderer lighting/shadow integration.
