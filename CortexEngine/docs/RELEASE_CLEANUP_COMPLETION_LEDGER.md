# Release Cleanup Completion Ledger

Audit date: 2026-05-12

This ledger defines the cleanup phase needed to present Cortex as a real,
reviewable public renderer project. It is a planning and completion tracker
only. The implementation work for these rows has not started in this checkpoint.

The cleanup phase is about public-facing proof: clear README language, real
screenshots, measured frame reports, high-resolution captures, package evidence,
and a clean main-branch handoff. It should not add new renderer features unless
a release-readiness gate proves that a presentation problem is caused by a
runtime bug.

## Status Values

- `DONE_VERIFIED`: implementation exists and the listed validation command has
  passed with current evidence.
- `DONE_UNVERIFIED`: implementation appears present, but proof is stale,
  indirect, or missing an explicit command/log.
- `PARTIAL`: some supporting implementation exists, but the public-release
  deliverable is incomplete.
- `NOT_STARTED`: no concrete implementation for this cleanup item exists yet.
- `BLOCKED`: missing assets, missing hardware, or missing product decisions
  prevent completion.
- `DEFERRED_BY_USER_ONLY`: explicitly deferred by user decision.

## Completion Gate

This cleanup phase is complete only when every ledger item below is
`DONE_VERIFIED` or `DEFERRED_BY_USER_ONLY`, and all commands pass from a current
Release build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_package_contract_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_package_launch_smoke.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_visual_probe_validation.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_phase3_visual_matrix.ps1 -NoBuild
git -c core.autocrlf=false diff --check --ignore-submodules=all
git status --short --branch --ignore-submodules=all
```

Additional cleanup-phase commands to create during implementation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine\docs\media
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_public_readme_contract_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_cleanup_ledger_tests.ps1
```

## Source Map

| Key | Files and systems |
|---|---|
| DOC-README | `CortexEngine/README.md`, `CortexEngine/RELEASE_READINESS.md`, `CortexEngine/tools/README.md`, `CortexEngine/BUILD.md`, `CortexEngine/SCRIPTS.md` |
| DOC-MEDIA | planned `CortexEngine/docs/media/`, planned public screenshot gallery and captions |
| DOC-METRICS | `frame_report_last.json`, `frame_report_shutdown.json`, `release_validation_summary.json`, visual probe summaries |
| CAPTURE | `tools/run_visual_baseline_contract_tests.ps1`, `tools/run_visual_probe_validation.ps1`, `tools/run_phase3_visual_matrix.ps1`, planned `tools/run_public_capture_gallery.ps1` |
| PACKAGE | `assets/config/release_package_manifest.json`, `tools/run_release_package_contract_tests.ps1`, `tools/run_release_package_launch_smoke.ps1` |
| VALIDATION | `tools/run_release_validation.ps1`, release smoke scripts, ownership tests, package tests, repo hygiene tests |
| BRANCH | git branch handoff, main-branch merge/push/tag steps |

## Public Positioning Rules

The public docs should describe Cortex plainly:

- Call it a real-time DirectX 12 hybrid renderer.
- Say what is implemented and validated, not what is aspirational.
- Mention AI tooling as integrated tooling, not as the core renderer identity.
- Do not present the voxel backend as the main product.
- Do not call planned effects, material parity expansions, or future polish
  complete unless their runtime gates are present.
- Use short, specific claims backed by screenshots, frame metrics, and scripts.
- Avoid marketing filler, exaggerated language, and generic AI-sounding copy.

## Ledger Items

| ID | Requirement | Status | Source | Validation | Evidence Required | Remaining Work |
|---|---|---|---|---|---|---|
| RC-01 | Define the cleanup phase and completion ledger for public release readiness. | DONE_VERIFIED | this file | Markdown review plus `git diff --check` | This ledger exists with status model, source map, gates, and concrete release cleanup rows. | Keep this ledger updated after every cleanup checkpoint. |
| RC-02 | Rewrite README opening so the project is clearly described without verbose or inflated language. | NOT_STARTED | DOC-README | planned `run_public_readme_contract_tests.ps1` | README lead should fit in a short paragraph plus concise bullets: renderer identity, DX12 hybrid path, material/RT/validation features, and how to run. | Draft and review README copy; remove internal roadmap noise from first viewport. |
| RC-03 | Add a compact "What It Shows" section with concrete feature proof. | NOT_STARTED | DOC-README, DOC-METRICS | planned README contract; visual gallery commands | README should list validated features with links to scripts/log evidence: RT reflections/GI, material lab, IBL, particles, graphics UI, package gate. | Write section from current validation facts, not aspirational text. |
| RC-04 | Add real screenshots to docs, not placeholders. | NOT_STARTED | DOC-MEDIA, CAPTURE | planned `run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media` | Committed images should exist for RT Showcase, Material Lab, Glass/Water Courtyard, Effects Showcase, Outdoor Sunset Beach, and IBL Gallery. | Implement capture gallery script and commit selected compressed public images. |
| RC-05 | Capture high-resolution showcase images, not only low-res smoke output. | NOT_STARTED | CAPTURE, renderer graphics presets | planned high-quality capture command; frame reports | High-res captures should record render size, render scale, scene, camera bookmark, environment, preset, and GPU/frame metrics beside each image. | Add a high-quality capture preset or script mode; verify it does not mutate release smoke thresholds. |
| RC-06 | Include a small metrics table in README using real current run data. | NOT_STARTED | DOC-README, DOC-METRICS | planned README contract; parse latest approved frame reports | README metrics should include GPU frame time, avg luma/nonblack, RT reflection signal/history, descriptor/memory budget, and package size from one approved run. | Extract metrics from a named release-validation log and cite the log path/date. |
| RC-07 | Document default run quality and high-quality showcase commands separately. | NOT_STARTED | DOC-README, DOC-METRICS, CAPTURE | planned README contract; manual command smoke | README should explain normal default startup may use safe/moderate settings, while public captures use explicit release/high-quality presets. | Add exact commands for default run, release showcase run, high-res capture, and validation. |
| RC-08 | Make public screenshots reproducible from scripts. | NOT_STARTED | CAPTURE | planned `run_public_capture_gallery.ps1`; `run_visual_probe_validation.ps1` | A single command should regenerate docs/media captures and write a manifest with scene, command, commit, timestamp, dimensions, and metrics. | Implement script and manifest schema; avoid committing logs or transient build output. |
| RC-09 | Add screenshot and metrics manifest to prevent stale README media. | NOT_STARTED | DOC-MEDIA, DOC-METRICS | planned `run_public_readme_contract_tests.ps1` | README image links and metrics table must match files/entries in `docs/media/gallery_manifest.json`. | Create manifest, wire README contract to check referenced files and timestamps. |
| RC-10 | Keep package manifest aligned with public docs. | PARTIAL | PACKAGE, DOC-README | `run_release_package_contract_tests.ps1 -NoBuild` | Existing package contract validates runtime payload and docs, but docs do not yet include final media/metrics policy. | Decide whether public screenshots are source docs or external release assets; update manifest accordingly. |
| RC-11 | Produce a public-review package artifact from the validated manifest. | NOT_STARTED | PACKAGE | planned packaging command plus existing package contract/launch smoke | A package archive or staged directory should be created, size checked, launch-tested, and recorded in the ledger. | Add or document final package creation command; do not include models, logs, build caches, HDR/EXR source files, or local artifacts. |
| RC-12 | Update RELEASE_READINESS with the final current validation, not stale numbers. | NOT_STARTED | DOC-README, VALIDATION | `run_release_validation.ps1`; planned README contract | Release readiness should cite the final run path/date, package proof, hardware note, and remaining known limitations. | Refresh from the final run after cleanup screenshots and package proof pass. |
| RC-13 | Add a short architecture summary that shows modularity without dumping internals. | NOT_STARTED | DOC-README | planned README contract | README should show Core, Graphics, Scene, UI, AI tooling, validation, and package flow in a concise structure. | Replace long internal details with a readable public architecture section and links to deeper docs. |
| RC-14 | Add a public "Validation" section that explains what the gates prove. | NOT_STARTED | DOC-README, VALIDATION | planned README contract | README should explain release validation, visual validation, package launch smoke, and repo hygiene in user-facing terms. | Keep the full gate list in `RELEASE_READINESS.md`; keep README concise. |
| RC-15 | Add "Limitations" wording that is honest and specific. | NOT_STARTED | DOC-README, RELEASE_READINESS | planned README contract | README should state hardware dependence, optional Dreamer/TensorRT path, experimental voxel backend, and remaining material/effects work. | Avoid hiding known partial ledger items; phrase them as scoped limitations. |
| RC-16 | Preserve existing validation while adding presentation assets. | NOT_STARTED | VALIDATION, DOC-MEDIA | `run_release_validation.ps1`; `git diff --check` | Full release validation should pass after docs/media changes; no tests weakened to allow screenshots or package updates. | Re-run release validation after media/docs/package edits. |
| RC-17 | Commit cleanup in reviewable checkpoints. | NOT_STARTED | BRANCH | `git log --oneline`, `git status --short --branch --ignore-submodules=all` | Separate commits should cover ledger, README/media, package, validation updates, and final handoff. | Do not squash unrelated renderer changes into presentation commits. |
| RC-18 | Merge or fast-forward the final cleanup state into `main`. | NOT_STARTED | BRANCH | `git branch --show-current`; `git log main..HEAD`; `git status --short --branch --ignore-submodules=all` | Final state should be on `main`, pushed, and traceable to the release-validation evidence. | Confirm branch policy, merge/fast-forward, push, and record commit hash. |
| RC-19 | Create final release tag or named checkpoint if requested. | BLOCKED | BRANCH | `git tag --list`; release package evidence | Requires user decision on tag name/version. | Ask for tag/version only after all cleanup gates pass. |
| RC-20 | Final public-review handoff summary. | NOT_STARTED | DOC-README, PACKAGE, VALIDATION, BRANCH | all completion gate commands | Final summary should include commit hash, package path, screenshot manifest, validation log path, known limitations, and exact run commands. | Produce only after every required row is verified or explicitly deferred. |

## Implementation Order

1. Create README contract tests before rewriting copy.
2. Rewrite README and RELEASE_READINESS in a compact public-facing style.
3. Implement the high-resolution public capture gallery script.
4. Generate and select screenshots, then write `docs/media/gallery_manifest.json`.
5. Add README images, captions, and a metrics table sourced from frame reports.
6. Decide package policy for media assets and update package manifest if needed.
7. Build and run full release validation.
8. Run package contract and staged package launch smoke.
9. Commit cleanup in coherent checkpoints.
10. Merge/fast-forward to `main`, push, and record final evidence.

## Non-Goals For This Cleanup Phase

- Do not add new renderer features.
- Do not loosen validation thresholds to make screenshots pass.
- Do not commit generated logs, build directories, local models, or source
  HDR/EXR environment assets.
- Do not claim the new materials/graphics robustness phase is complete unless
  its separate ledger is also complete.
- Do not merge to `main` until the final release gate and package launch smoke
  both pass from the exact committed state.
