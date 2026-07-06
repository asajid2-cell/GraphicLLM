# GenScene Queue

`CURRENT.md` wins for state. This file records queue items and accept/reject outcomes.

## Active Queue

1. Phase 0 freeze/checkpoint: reject or accept the current Loop 43 dirty tree through
   `tools/run_genscene_acceptance.ps1`, then commit the clean campaign-state scaffold.
2. Phase 1 curation: add `.gitignore` protections and `tools/curate_gallery.py`.
3. Phase 2 gate reset: split semantic/render-health/judge checks and demote old pixel metrics.
4. Phase 3 first structural front: continuous terrain in generated exteriors.

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
