# GenScene Next-Level Plan — post-mortem of the AAA push and the road out

Date: 2026-07-06. Written after reviewing the full Codex rollout
(`rollout-2026-07-03T00-53-28`, 958MB, 71 compactions, Loops ~1-43 of the AAA graphics
push), the campaign ledgers, the current dirty worktree, and the actual rendered stills.

## 1. Verdict on the current direction

**The process discipline is excellent. The strategy is stuck in a local minimum, and the
renders prove it.**

What the agent did well:
- Red/green verifier discipline: every loop proved the new gate fails old artifacts
  before implementing. Thresholds were never weakened to pass.
- Honest ledgering: every checkpoint ends with "visual truth remains negative — do not
  present as AAA." No fake victories.
- Real infra wins along the way: per-render frame-report sidecars, runtime receipts,
  VB/deferred pipeline evidence, per-render `.out` logs.

What went wrong — the Goodhart tower:
- 43 loops each added (a) a new pixel-statistic hard-fail code to
  `tools/scene_graphics_gate.py` (now **3,331 lines, 48 `missing_*` codes**) and (b) a
  new runtime "pass" in `src/Core/Engine_Scenes.cpp` (now **26,113 lines, 44
  `Generative*` pass structs**) whose only job is to satisfy that statistic.
- The gates measure proxies: `water_edge_density`, `lower_flat_sheet_fraction`,
  `smooth_card_fraction`, `ground_vertical_detail`, `dark_contact_area_fraction`,
  `frame_nonblack_fraction`. The passes became an adversarial generator against those
  proxies. Edge density → crinkle-foil water. Anti-flat-sheet → shredded-paper terrain
  shards. Vertical detail → stick clutter. Contact evidence → black blobs.
- Net visual result after ~40 loops: the stills are **more** incoherent than the Loop-1
  baseline. `aaa_loop43_patch_campsite_f_0.png` is a field of overlapping glossy slabs
  with a stick-frame "tent"; `aaa_loop42_sheet_desert_bo_0.png` is torn-paper shards
  around a luminous cyan rectangle; `aaa_loop42_sheet_alpine_bm_0.png` has a full-frame
  ghost reflection overlay smeared across the image.
- The ledger itself names the real ceiling repeatedly (Loops 7, 10, 18, 21, 26, 28, 29:
  "the next serious front is better assets / real materials, not another metric-only
  proof layer") — but the loop structure (never remove a gate, every render must pass
  all prior codes) ratchets toward more overlay geometry and makes the pivot
  structurally impossible. The gates are now the problem, not the safety net.
- The engine's actual strengths — DXR reflections, IBL/HDRI, Gerstner-wave
  `WaterSubsystem`, terrain heightfield + FBM generators (exist, unwired), volumetric
  fog/god rays — are barely used by the generated path. "Lighting" was re-implemented
  as cards and "texture" as grain strips inside `Engine_Scenes.cpp`.

The other root ceiling: **assets**. 511 catalog entries are mostly Kenney flat-shaded
low-poly kits, 6 Poly Haven PBR texture sets, 22 naturalistic models. No overlay pass
can make Kenney kits read AAA.

## 2. Current defects (bugs, mess, redundancy)

### Visual bugs (visible in latest stills)
- B1. Alpine full-frame ghost overlay: translucent window/plank reflections smeared
  across the whole frame (SSR or a reflection-card pass leaking). `aaa_loop42_sheet_alpine_bm_0.png`.
- B2. Baked vignette/border frame on campsite renders — reads as an Instagram filter.
- B3. White speckle "particles" floating over sky/mountains in campsite and desert.
- B4. Foreground terrain rendered as overlapping torn shards / plank slabs (the Loop
  38-43 patch-card stack fighting itself).
- B5. Water as glossy foil sheets / luminous flat rectangles; shorelines are strip
  artifacts.
- B6. Hero objects: stick-frame tent with floating cube (campsite); low-poly wedge tent
  (desert); scale inconsistencies between heroes and terrain.
- B7. Mid-loop dirty worktree: 12 modified files (both HLSL shaders and
  `Engine_Scenes.cpp`) from the half-finished Loop 43; the last render still failed the
  loop's own gate (`smooth=0.52` vs cap `0.50`).

### Repo / media mess (the "sloppy and generated, not curated" problem)
- M1. `docs/media/final_art/model_authored/`: **5,883 machine-named `.bmp` files**
  (`...slot_content_v_2b624c2353.bmp`, `...v37__view_hoop_contact_side.bmp`) —
  uncompressed BMPs, pipeline debris living under `docs/`. Untracked, but they make the
  tree look like a dump and are one bad `git add` away from being committed.
- M2. Repo root: ~40 `build_*.log` junk files (`build_kit3.log`, `build_godray2.log`…).
- M3. `build/bin/logs`: 4,595 loop artifacts with names like
  `aaa_loop42_sheet_alpine_bj_cam2exp15.png` — no promotion path from "loop artifact"
  to "curated result", so anything shown from here reads as generated slop.
- M4. No gen-scene gallery at all: README showcases only the handcrafted scenes
  (rt_showcase, material_lab, …). The entire GenScene capability has zero curated
  presentation — the flagship feature is invisible or, worse, represented by loop
  debris.
- M5. Ledger sprawl: 8+ overlapping ledger/plan docs (`CAMPAIGN.md`,
  `CAMPAIGN_AAA_GRAPHICS.md`, `LOOPS.md`, `LOOPS_AAA_GRAPHICS.md`, plus 6 in `docs/`).
- M6. Naming leakage: internal codenames (`aaa_loop43_patch_*`, `sak_*`,
  `family_constructor_hydrated`) leak into filenames that end up in media folders.

### Code redundancy / structural debt
- R1. `Engine_Scenes.cpp` 26k lines; 44 generative pass structs, several mutually
  antagonistic (Loop 42 sheet-realism vs Loop 43 continuity literally fought each other
  across the final hours of the session; dead branches added then excised).
- R2. `scene_graphics_gate.py` 3.3k lines / 48 stacked hard-fail codes — unfalsifiable
  in aggregate; the ratchet forbids ever deleting a pass.
- R3. Overlapping verifier tools: `scene_quality_gate.py`, `scene_graphics_gate.py`,
  `scene_battery.py`, plus six `analyze_*.py` one-offs.
- R4. Per-prompt-family hard-coded camera profiles (alpine `pos=(3.45,1.33,8.23)` etc.)
  — brittle special-casing that masks the absence of a composition system.

## 3. The plan

### Phase 0 — Checkpoint and stop the bleeding (half a day)
1. Resolve the Loop 43 dirty state: either finish the shader-level breakup cleanly or
   revert to the Loop 42 checkpoint. Do not leave shaders half-patched. Commit + tag
   (`aaa-push-loop43-frozen`).
2. **Freeze the gate ratchet**: no new `missing_*` codes. Declare the AAA-overlay
   campaign closed in `CAMPAIGN_AAA_GRAPHICS.md` with this post-mortem linked.

### Phase 1 — Curation & hygiene (1 day) — the user's #1 complaint
1. Delete (regenerable) or quarantine to a non-repo scratch dir:
   `docs/media/final_art/model_authored/*`, root `build_*.log`, `gltf_probe.obj`.
   Add `.gitignore` entries: `build_*.log`, `docs/media/final_art/`, `build/bin/logs/`.
2. Create the **promotion pipeline**: `tools/curate_gallery.py` — takes a render from
   `build/bin/logs/`, re-encodes to PNG at a canonical resolution, renames to a stable
   human name (`genscene_campsite_dawn_hero.png`), records prompt/seed/settings into
   `docs/media/genscene/manifest.json`. **Nothing enters `docs/media/` any other way.**
   Curated = picked by a human (or a held-out judge with human confirm), best-of-N,
   consistent aspect ratio, no loop codenames, no vignettes/watermarks.
3. Collapse ledgers: one `CAMPAIGN.md` (live) + `docs/archive/` for the rest.

### Phase 2 — Tear down the Goodhart tower (1-2 days)
1. Replace the 48 pixel-proxy gates with three layers:
   - **Semantic gate** (keep, from `scene_quality_gate.py`): prompt entities present,
     colors correct, hero visible.
   - **Render-health gate** (small): renders without crash/timeout, sane luma
     histogram, frame-report sidecar present, no NaN/black frame.
   - **Held-out judge**: a VLM critique with a fixed rubric (composition, coherence,
     material believability, lighting, artifact scan) that returns scored verdicts and
     has veto power — the design-iterate pattern. Pixel statistics may remain as
     *telemetry*, never as hard gates.
2. Delete the overlay passes that exist only to feed dead metrics: grain strips,
   shadow-band cards, patch cards, sheet masks, split ribbons, proof scatter,
   contact-disk layers. Keep genuinely structural work (terrain tessellation, hero
   construction, source-asset placement, camera/exposure plumbing).
   Target: `Engine_Scenes.cpp` generative path shrinks by thousands of lines and the
   stills get *cleaner*, not worse — that is itself the proof the tower was the problem.
3. Re-render the three canonical prompts after the purge as the new visual baseline.

### Phase 3 — The real quality ladder (the actual "next level", ~1-2 weeks of loops)
Ordered by visual leverage per effort:
1. **Continuous terrain**: wire the existing heightfield + FBM generators
   (`MeshGenerator.h`, `TerrainNoise`) into the generated-exterior path. One displaced,
   splat-mapped terrain mesh (Poly Haven `aerial_grass_rock`, `coast_sand_05`, + fetch
   ~6 more CC0 sets: cliff, forest floor, snow, gravel, desert rock). Kills B4/B5's
   foreground shards permanently because there are no more cards.
2. **Real water**: route generated lakes/rivers through `WaterSubsystem`
   (Gerstner waves, depth tint, foam, shore blending) — the machinery exists and the
   handcrafted beach scene already uses it. Delete card-water.
3. **Sky & lighting as authored intent**: per-prompt HDRI or procedural sky
   (time-of-day from the Director), sun direction/color/exposure driven from IR,
   volumetric fog / god rays from the existing renderer controls. No atmosphere cards.
4. **Asset ladder** (the hard ceiling — start immediately, runs in parallel):
   a. Poly Haven CC0 photoreal models (rocks, trees, stumps, camping props) via the
      existing fetch/normalize pipeline (.gltf + external .bin, draco-decoded).
   b. Curated Sketchfab set through `fetch_sketchfab.mjs` (tents, cabins, canoes with
      PBR textures) — build a reviewed `nature_photoreal` pack the catalog scans.
   c. Kenney kits demoted to an explicit "stylized" mode; never mixed into
      photoreal prompts.
5. **Composition system instead of camera hacks**: rule-of-thirds hero placement,
   depth layering (foreground anchor / midground hero / background massing), scale
   sanity checks (tent ≈ 2m, cabin ≈ 6m), horizon placement — replacing per-family
   hard-coded camera positions.
6. **Flagship renderer features in the money shots**: DXR reflections on water,
   SSGI/SSAO tuned per scene, high-quality capture mode (supersampled, sequential) for
   gallery promotion. A 3070 Ti handles this for stills.
7. Per-scene polish loops (best-of-N + judge veto) only *after* 1-6, when the loop is
   polishing real content instead of decorating cards.

### Phase 4 — Presentation (after first Phase-3 wins)
- README GenScene section: prompt → curated still pairs, an honest before/after strip
  (Loop-1 baseline vs current), 30-60s reel. /rereadme pass to strip jargon and loop
  codenames from everything public.

### Process guardrails (so this doesn't recur)
- Any campaign gets a **strategy review every 5 loops**: "did the last 5 loops improve
  the *image* or the *metric*?" — with authority to delete gates, not just add.
- Hard cap on pixel-statistic hard-gates (≤ ~6); everything else is telemetry.
- A gate that has been green for 3 consecutive loops while visual truth stays negative
  is presumed Goodharted and demoted.
- Sessions: don't let a single rollout run to 71 compactions; checkpoint + fresh
  session per front.

## 4. Success criteria
- `docs/media/` contains only curated, human-named, manifest-backed media.
- The three canonical prompts render with: continuous textured terrain, subsystem
  water, authored sky, ≥1 photoreal hero asset, no cards/shards/speckles/ghost overlay.
- The held-out judge scores ≥ "good" on all rubric axes for best-of-N picks, and a
  cold human look agrees.
- `Engine_Scenes.cpp` generative path and `scene_graphics_gate.py` both materially
  smaller than today.
