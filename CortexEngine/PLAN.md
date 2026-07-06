# GenScene Plan

This file is strategy only. State and gate results live in `CURRENT.md`.

## Phase 0: Freeze The Overlay Campaign

- Resolve the dirty Loop 43 production diff through the acceptance runner.
- Do not add new pixel hard gates.
- Close the old AAA overlay loop as historical evidence, not the active route.

## Phase 1: Curation And Hygiene

- Add a promotion pipeline for chosen renders: stable human filenames, canonical PNG output,
  prompt/settings manifest, and no loop codenames.
- Quarantine or ignore generated media debris and root build logs.
- Make `docs/media` contain curated media only.

## Phase 2: Gate Reset

- Keep semantic/prompt correctness checks.
- Replace the stacked pixel-proxy graphics gate with a small render-health gate plus a held-out
  visual judge rubric.
- Demote edge-density and flat-sheet metrics to telemetry.
- Delete overlay passes that only feed obsolete metrics.

## Phase 3: Real Scene Quality Ladder

1. Wire continuous heightfield/FBM terrain into generated exteriors.
2. Route generated lakes and rivers through the real water subsystem.
3. Drive sky, sun, fog, exposure, and god rays from Director IR.
4. Build a photoreal nature/campsite asset ladder and demote Kenney assets to stylized mode.
5. Replace hard-coded camera profiles with a composition system.
6. Use high-quality sequential still capture, including DXR/SSGI where appropriate, for curated
   gallery shots.

## Process Guardrails

- Every fifth loop gets a strategy review: did the image improve, or only the metric?
- Keep hard pixel gates under a small cap; everything else is telemetry.
- A gate that stays green while visual truth stays negative is presumed Goodharted and demoted.
- Start fresh sessions per major front; do not run another 70-compaction rollout.
