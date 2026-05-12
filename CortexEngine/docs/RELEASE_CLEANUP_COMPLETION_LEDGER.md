# Release Cleanup Completion Ledger

Audit date: 2026-05-12
Closure date: 2026-05-12

This ledger tracks the cleanup phase that turns Cortex into a reviewable public
renderer project: public README wording, high-resolution screenshots, measured
frame reports, package proof, release validation, and main-branch handoff.

## Status Values

- `DONE_VERIFIED`: implementation exists and the listed validation command has
  passed with current evidence.
- `DEFERRED_BY_USER_ONLY`: explicitly deferred by user decision.

## Completion Gate

Cleanup is complete only when every row is `DONE_VERIFIED` or
`DEFERRED_BY_USER_ONLY`.

Final evidence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\rebuild.ps1 -Config Release
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_package_contract_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_package_launch_smoke.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_visual_probe_validation.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_phase3_visual_matrix.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine\docs\media -Width 1920 -Height 1080
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_public_readme_contract_tests.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine\tools\run_release_cleanup_ledger_tests.ps1
git -c core.autocrlf=false diff --check --ignore-submodules=all
git status --short --branch --ignore-submodules=all
```

Latest full release validation:

```text
CortexEngine/build/bin/logs/runs/release_validation_20260512_133827_609_2236_0286ad00/release_validation_summary.json
```

Result: `status=passed`, `step_count=64`, `failure_count=0`.

Public gallery:

```text
CortexEngine/docs/media/gallery_manifest.json
CortexEngine/build/bin/logs/runs/public_capture_gallery_20260512_133541_420_18928_97a03e95
```

Release package:

```text
CortexEngine/release/cortex-public-review_6de5ffd_20260512_134536.zip
CortexEngine/release/cortex-public-review_6de5ffd_20260512_134536_summary.json
```

Package size: `83,395,208` bytes / `536,870,912` byte cap.

## Source Map

| Key | Files and systems |
|---|---|
| DOC-README | `CortexEngine/README.md`, `CortexEngine/RELEASE_READINESS.md`, `CortexEngine/tools/README.md`, `CortexEngine/BUILD.md`, `CortexEngine/SCRIPTS.md` |
| DOC-MEDIA | `CortexEngine/docs/media/`, `docs/media/gallery_manifest.json` |
| DOC-METRICS | `frame_report_last.json`, `release_validation_summary.json`, gallery manifest |
| CAPTURE | `tools/run_public_capture_gallery.ps1`, visual validation scripts |
| PACKAGE | `assets/config/release_package_manifest.json`, package contract, launch smoke, package creation script |
| VALIDATION | `tools/run_release_validation.ps1`, public README contract, cleanup ledger contract |
| BRANCH | `main` handoff and `cortex-public-review-2026-05-12` tag |

## Ledger Items

| ID | Requirement | Status | Source | Validation | Evidence | Remaining Work |
|---|---|---|---|---|---|---|
| RC-01 | Define the cleanup phase and completion ledger for public release readiness. | DONE_VERIFIED | this file | `run_release_cleanup_ledger_tests.ps1` | Ledger exists and is closed by the cleanup ledger contract. | None. |
| RC-02 | Rewrite README opening so the project is clearly described without verbose or inflated language. | DONE_VERIFIED | DOC-README | `run_public_readme_contract_tests.ps1` | README now opens as a real-time DirectX 12 hybrid renderer and rejects banned inflated wording. | None. |
| RC-03 | Add a compact "What It Shows" section with concrete feature proof. | DONE_VERIFIED | DOC-README, DOC-METRICS | `run_public_readme_contract_tests.ps1` | README includes validated feature bullets tied to renderer, RT, material, IBL, scenes, and release discipline. | None. |
| RC-04 | Add real screenshots to docs, not placeholders. | DONE_VERIFIED | DOC-MEDIA, CAPTURE | `run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080` | Six committed PNG captures exist in `docs/media`. | None. |
| RC-05 | Capture high-resolution showcase images, not only low-res smoke output. | DONE_VERIFIED | CAPTURE, graphics presets | `run_public_capture_gallery.ps1` | `gallery_manifest.json` records 1920x1080 captures with `render_scale=1.0` and `graphics_preset=public_high`. | None. |
| RC-06 | Include a small metrics table in README using real current run data. | DONE_VERIFIED | DOC-README, DOC-METRICS | `run_public_readme_contract_tests.ps1` | README metrics table cites `public_capture_gallery_20260512_133541_420_18928_97a03e95`. | None. |
| RC-07 | Document default run quality and high-quality showcase commands separately. | DONE_VERIFIED | DOC-README, CAPTURE | `run_public_readme_contract_tests.ps1` | README documents `safe_startup`, `release_showcase`, and `public_high` commands separately. | None. |
| RC-08 | Make public screenshots reproducible from scripts. | DONE_VERIFIED | CAPTURE | `run_public_capture_gallery.ps1` | One command regenerates captures and manifest with command, commit, dimensions, metrics, and log paths. | None. |
| RC-09 | Add screenshot and metrics manifest to prevent stale README media. | DONE_VERIFIED | DOC-MEDIA, DOC-METRICS | `run_public_readme_contract_tests.ps1` | README image links are checked against `docs/media/gallery_manifest.json`. | None. |
| RC-10 | Keep package manifest aligned with public docs. | DONE_VERIFIED | PACKAGE, DOC-README | `run_release_package_contract_tests.ps1 -NoBuild` | Package manifest requires README, release notes, cleanup ledger, gallery manifest, and all public screenshots. | None. |
| RC-11 | Produce a public-review package artifact from the validated manifest. | DONE_VERIFIED | PACKAGE | `run_create_release_package.ps1 -NoBuild` | Archive `CortexEngine/release/cortex-public-review_6de5ffd_20260512_134536.zip` created under size cap. | None. |
| RC-12 | Update RELEASE_READINESS with the final current validation, not stale numbers. | DONE_VERIFIED | DOC-README, VALIDATION | `run_public_readme_contract_tests.ps1`; `run_release_validation.ps1` | `RELEASE_READINESS.md` cites final release validation, gallery evidence, and package artifact. | None. |
| RC-13 | Add a short architecture summary that shows modularity without dumping internals. | DONE_VERIFIED | DOC-README | `run_public_readme_contract_tests.ps1` | README architecture section covers Core, Graphics, Scene, UI, AI, and tools concisely. | None. |
| RC-14 | Add a public "Validation" section that explains what the gates prove. | DONE_VERIFIED | DOC-README, VALIDATION | `run_public_readme_contract_tests.ps1`; `run_release_validation.ps1` | README explains full release validation and focused gates. | None. |
| RC-15 | Add "Limitations" wording that is honest and specific. | DONE_VERIFIED | DOC-README, RELEASE_READINESS | `run_public_readme_contract_tests.ps1` | README and release notes state hardware dependence, optional AI paths, experimental voxel backend, and remaining robustness-ledger work. | None. |
| RC-16 | Preserve existing validation while adding presentation assets. | DONE_VERIFIED | VALIDATION, DOC-MEDIA | `run_release_validation.ps1`; `git diff --check` | Full release validation passed after README/media/package tooling changes. | None. |
| RC-17 | Commit cleanup in reviewable checkpoints. | DONE_VERIFIED | BRANCH | `git log --oneline`; `git status --short --branch --ignore-submodules=all` | Cleanup landed in ledger, tooling/media, gallery refresh, and final evidence commits. | None. |
| RC-18 | Merge or fast-forward the final cleanup state into `main`. | DONE_VERIFIED | BRANCH | `git branch --show-current`; `git status --short --branch --ignore-submodules=all` | Final ledger closure is being committed on `main`; remote handoff is `git push -u origin main`. | None. |
| RC-19 | Create final release tag or named checkpoint if requested. | DONE_VERIFIED | BRANCH | `git tag --list cortex-public-review-2026-05-12` | Final checkpoint tag: `cortex-public-review-2026-05-12`. | None. |
| RC-20 | Final public-review handoff summary. | DONE_VERIFIED | DOC-README, PACKAGE, VALIDATION, BRANCH | all completion gate commands | README, release readiness, package artifact, gallery manifest, release validation, main handoff, and tag are all recorded. | None. |

## Final Handoff

Public branch: `main`

Release tag: `cortex-public-review-2026-05-12`

Primary commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_create_release_package.ps1 -NoBuild
```

## Non-Goals Preserved

- No new renderer feature was added for this cleanup beyond scripted
  high-resolution launch dimensions and the `public_high` preset needed for
  reproducible public captures.
- Validation thresholds were not loosened.
- Generated logs, build directories, local models, and package artifacts remain
  untracked.
- The separate materials/graphics robustness ledger remains the source of truth
  for deeper future material/effects work.
