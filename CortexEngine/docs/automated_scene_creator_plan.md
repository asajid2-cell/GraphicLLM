# Automated Scene Creator — Plan

Goal: a real automated scene creator that turns intent (a preset name or a free-text
prompt) into a believable scene built from **real catalogued assets** — unifying the
hardcoded hero scenes, the procedural recipes, and the asset catalog, with an optional
generative network on top. No primitive stand-ins, no fake "AI".

## Where we are (done)
- **AssetCatalog** (`src/Scene/AssetCatalog.*`) — real tagged library (142 Kenney +
  registry), `ResolvePath` by id/role/family, measured footprints.
- **SceneRecipes** (`src/LLM/SceneRecipes.*`) — procedural indoor rooms (living_room/
  bedroom/office/kitchen) that place real meshes with self-calibrated scale, rotation,
  ground-snap, and a tiled wall shell. Reachable by text/env/CLI; LLM-independent.
- **Beach rebuild** — `BuildOutdoorSunsetBeachScene` re-authored to drop ~860 lines of
  primitive scatter and place only the real `naturalistic_showcase` meshes
  (boulder/driftwood/stump/fern/grass/bush), ground-snapped, water-excluded, clustered.
  First real test of the asset-driven approach on an OUTDOOR scene.

## Asset-gap analysis (outdoor)
- Indoor is well covered (Kenney). Outdoor relies on `naturalistic_showcase` (~11 nature
  meshes) — enough for a beach/forest dressing but thin. Gaps: palms, beach furniture
  (umbrella/chair), large terrain rock variety. Action: fold `naturalistic_showcase`
  (and any future packs) into AssetCatalog with an `outdoor`/`nature` role taxonomy so
  recipes resolve them the same way as furniture.
- Placement difference that matters: indoor = discrete furniture against walls/grid;
  outdoor = scatter with **ground-conform** (snap to terrain height, not y=0), **exclusion
  zones** (water, paths, sightlines), and **clustering/density falloff** (rocks in groups,
  grass in bands) — not uniform random. The current beach uses flat y=0 + manual clusters;
  a real terrain needs height-sampled conform.

## Target architecture — a SceneSpec + layout layer
Unify the three content sources behind one data model so hero scenes, recipes, and LLM
output all flow through the same placement/validation path:

1. **SceneSpec** (data) — environment (lighting/sky/water/fog presets), camera, and a list
   of *placements* / *scatter regions* / *room-shell* directives referencing **catalog
   ids or roles** (never raw meshes/primitives).
2. **Layout/placement engine** — turns a SceneSpec into ECS entities: catalog resolve →
   measure → scale-normalize → ground-conform → collision/exclusion → emit. (Generalizes
   the recipe helpers `Place`/`PlaceExplicit`/`BuildRoomShell` + the beach `placeNature`.)
3. **Sources that produce a SceneSpec**:
   - Hero presets (`Engine_Scenes`) — migrate from hand-placed entities to authored
     SceneSpecs (incremental; beach/rooms first).
   - Procedural recipes — already produce placements; reframe as SceneSpec emitters.
   - LLM Architect — emits a SceneSpec (or the existing JSON commands) constrained to the
     catalog.
4. **Validation** — every placement must resolve to a real asset and pass bounds/overlap/
   exclusion checks; failures drop to a deterministic recipe, never to a primitive.

## Generative network integration
- The engine ships a llama.cpp "Architect" (gated `CORTEX_ENABLE_LLM_BACKEND=OFF`; GGUF
  models on disk) that emits JSON scene commands, plus a "Dreamer" texture-gen service.
- Flow: prompt → Architect → JSON commands/SceneSpec → **grounding pass** (rewrite/reject
  any asset id not in the catalog; map free-text nouns → catalog roles) → layout engine →
  scene. On low confidence / parse failure / empty → deterministic recipe for the matched
  room/biome (never primitives).
- Grounding is the key to "robust": inject the catalog vocabulary (real ids + roles) into
  the system prompt, and post-validate so the model can only place things we actually have.
- Dreamer (optional, later): generate PBR/sky textures for catalog meshes that lack them
  (most Kenney/nature meshes are untextured) to raise fidelity.

## Staged milestones
- **M1 (done):** AssetCatalog + indoor recipes + beach rebuild (asset-driven, verified by
  headless self-test + your visual check).
- **M2:** Fold `naturalistic_showcase` into AssetCatalog (nature roles); add a `beach`/
  `forest` outdoor recipe using scatter+exclusion so outdoor scenes are recipe-driven too.
- **M3:** SceneSpec data model + layout engine; migrate beach + one room to emit SceneSpec;
  add bounds/overlap/exclusion validation.
- **M4:** Turn on the llama.cpp Architect behind the catalog-grounding pass; prompt →
  grounded SceneSpec → scene, with recipe fallback. Headless grounding tests.
- **M5:** Dreamer texturing pass + camera auto-framing for generated scenes.

## Top risks
- **No visual verification in the automation shell** (windowed engine can't init headless)
  → rely on headless resolve/placement self-tests + the user's desktop visual loop; build a
  small "scene audit" self-test (counts, bounds, overlaps, floating/in-water checks).
- **LLM ungrounded output** (invents asset names) → hard grounding/validation + fallback.
- **Outdoor realism** (scatter looks fake without ground-conform/clustering/density) → the
  layout engine must own conform+exclusion+clustering, not ad-hoc per scene.
- **Asset thinness outdoors** → catalog more packs; Dreamer for textures.
