# Real Scene Creator — Design

Replaces the fake "scene creator" (12 hardcoded scene families + a keyword router +
an `UNAVAILABLE` model stub that fell back to primitive stand-ins — cylinder palm
trees, cube furniture). Built in tandem (Claude lead on source/architecture, Codex
on empirical build/runtime verification).

## Problem with what existed

- **Generation backend stubbed.** `LLMServiceStub` returns `{"commands":[]}`; the real
  `llama.cpp` path is gated `-DCORTEX_ENABLE_LLM_BACKEND=OFF`. GGUF models *are* on disk
  (`assets/Llama-3.1-8B`, `3.2-3B`, `tinyllama`).
- **Real assets not wired to generation.** `assets/final_art/asset_registry_v2.json`
  (33 tagged) + ~142 Kenney `.gltf` exist, but scene commands spawned **primitives**;
  the only real-mesh path was the Khronos sample library (`LoadSampleModelMesh`).
- Result: "AI" either no-ops or emits keyword-matched cubes/cylinders.

## Architecture — 3 layers, robust-by-default (not LLM-dependent)

The foundation is **deterministic** (real assets + real layout). The LLM is an optional
natural-language frontend on top; when it is absent/uncertain the system degrades to
procedural recipes — **never** back to primitive stand-ins.

### Layer 1 — AssetCatalog (foundation) — IN PROGRESS
`src/Scene/AssetCatalog.{h,cpp}`. A real, queryable, tagged library:
- Loads `asset_registry_v2.json` (rich `semantic_roles` + `scene_families`).
- Scans **all** `assets/models/kenney_furniture_kit/<name>/<name>.gltf` (~142 meshes);
  folder name = semantic id; a keyword map assigns coarse roles (`seating`, `storage`,
  `lighting`, `bathroom`, …) so role queries cover the bulk library too.
- Merges the two sources by id (registry path/tags win; roles unioned).
- `ResolvePath(key, variantSeed)` → real `.gltf` path. Match order: exact id →
  semantic role → scene family → fuzzy substring. `variantSeed` rotates among matches.
- Root discovery handles running from `build/bin` (walks up for the `assets/` marker;
  `CORTEX_ASSET_ROOT` override).

**Wired into** `CommandQueue::ExecuteAddEntity` Model branch: `asset="chair"` now
resolves a real Kenney mesh via `LoadGLTFMesh`; Khronos sample lib is the fallback;
primitive is the last resort. Kills the primitive-stand-in problem for catalogued assets.

### Layer 2 — Layout / placement engine + procedural recipes — NEXT
- Robust placement: floor snap (use mesh AABB min.y), wall alignment, collision-aware
  packing (extend the existing spiral search in `ExecuteAddEntity`), region/room layout
  (build on `ScenePlanCommand` + the region builders already in `CommandQueue`).
- Parameterized scene recipes that compose **real** assets via the catalog + layout
  (e.g. `classroom(rows, cols)`, `kitchen`, `living_room`) — the genuine generative
  layout that was never built. Deterministic, testable, model-free.

### Layer 3 — LLM frontend (optional enhancement) — LATER
- Enable `-DCORTEX_ENABLE_LLM_BACKEND=ON` (GGUF on disk), extend the Architect system
  prompt (`src/LLM/Prompts.h`) with the catalog vocabulary (real asset ids/roles) so a
  free-text prompt emits `add_entity{type:model, asset:<catalog id>}` + `scene_plan`
  commands. On low confidence / parse failure → fall through to Layer-2 recipes.

## Key seams (file:line)
- `src/LLM/CommandQueue.cpp` `ExecuteAddEntity` Model branch — catalog resolution.
- `src/Utils/GLTFLoader.{h,cpp}` `LoadGLTFMesh` — **risk:** header says "single mesh /
  single primitive"; Kenney furniture is often multi-primitive. Multi-primitive support
  may be a prerequisite (under empirical verification by Codex).
- `src/Scene/SceneIR.*`, `SceneTransaction.*` — semantic scene graph for advanced
  provenance/validation (optional).
- `src/LLM/Prompts.h` — Architect system prompt + JSON command schema.

## Build / verify
`rebuild.ps1 -Config Release` (imports VsDevCmd; `cl` on PATH without INCLUDE falsely
"succeeds"). Ground-truth = a real Kenney chair renders where a cube used to be.
