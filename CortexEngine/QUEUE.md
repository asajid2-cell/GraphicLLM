# GenScene Queue

`CURRENT.md` wins for state. This file records queue items and accept/reject outcomes.

## Active Queue

1. Phase 3 first structural front: continuous terrain in generated exteriors.

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
