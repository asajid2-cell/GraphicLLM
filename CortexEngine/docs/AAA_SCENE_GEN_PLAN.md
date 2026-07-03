# AAA Scene Generation Plan

## Target

The target is a prompt-to-scene director, not a catalog object scatterer. A prompt such as:

```powershell
python tools\scene_gen.py "a foggy mountain campsite beside a purple lake at dawn"
```

must compile into an art-directed scene with terrain, waterbody, atmosphere, materials, lighting, camera, composition, and dressing density. The current system produces a valid render, but not a high-fidelity scene: it asks an LLM for a loose object list, repairs that list into loadable assets, scatters them, then renders. That architecture cannot reliably create AA/AAA scenes.

## Architecture

The new pipeline should be:

```text
prompt
  -> Director planner
  -> Director IR v3
  -> deterministic compilers
       world/terrain compiler
       shot grammar compiler
       asset/material resolver
       lighting/atmosphere compiler
       render/camera compiler
  -> engine scene IR / runtime commands
  -> render
  -> quality gate
  -> targeted repair loop
```

The LLM should author intent, priorities, and semantic choices. Deterministic compilers should own geometry, lighting, scatter, material knobs, camera, and validation. Production tools follow this separation: Unreal PCG is point/attribute/mask driven rather than a raw object list; SideFX heightfields use terrain layers and masks for terrain/scatter; OpenUSD separates scene description, composition, and instancing. Sources:

- Unreal PCG overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview
- Unreal Lumen lighting/reflection docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
- Unreal volumetric fog docs: https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-fog-in-unreal-engine
- Unreal landscape materials: https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-materials-in-unreal-engine
- SideFX heightfields: https://www.sidefx.com/docs/houdini/model/heightfields.html
- SideFX HeightField Scatter: https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_scatter.html
- OpenUSD scenegraph instancing: https://openusd.org/release/api/_usd__page__scenegraph_instancing.html

The external fan-out adds one important correction: v3 should not emit hundreds of concrete object records as its primary representation. It should emit prototypes, spatial regions, generation passes, scatter rules, and instance groups. The compiler can still lower those to current v2 object entries temporarily, but the v3 source of truth must retain graph/mask/seed/prototype information so scenes can be regenerated, edited, streamed, and budgeted.

## Repo-Grounded Architecture Decisions

Fan-out lane B found the safest migration path:

```text
prompt -> Director IR v3 -> scene compiler -> existing v2 Scene IR -> render_ir.ps1 -> C++ generative recipe
```

Do not push v3 straight into the C++ engine first. Keep `CORTEX_SCENE_IR_JSON` as the runtime contract while v3 matures. The compiler should emit the current v2 IR first, then introduce new engine fields only when v2 cannot express the needed feature.

Fan-out lane A found the renderer is more capable than the current generator bridge:

- Existing renderer controls cover exposure, auto-exposure, bloom, IBL, environment/background exposure and rotation, color/tone grade, cinematic post, SSAO/SSR, fog, water, god rays, ray tracing, sun, vegetation/wind, ECS lights, rich PBR material fields, particles, and reflection probes.
- Weak bridge: the Director-facing command layer does not expose enough of those controls yet.
- Missing or weak authoring: clouds/weather, detailed volumetric parameters, shoreline/water masks, caustics/foam masks, bathymetry, local wave zones, and multi-probe reflection capture.

Fan-out lane C found the asset/material ceiling:

- Runtime catalog has roughly 511 loadable assets, enough for a blockout campsite but not AAA.
- Missing target categories include mountain backdrops, shoreline ecology, docks/piers, camping clutter, dense ground scatter, terrain decals, seasonal variants, LODs, collision proxies, and biome material sets.
- Runtime material components are rich, but glTF ingest/material normalization is weak: merged multi-material assets collapse toward one material path, and the registry has 0 AAA-ready assets by its own evidence.

Therefore the plan has two tracks:

1. **Director/compiler track:** produce better scenes now using existing controls and compile v3 to v2.
2. **Renderer/asset widening track:** expose more renderer controls and build a normalized AAA asset/material registry as v3 proves which controls/assets matter.

## Director IR v3

V3 should be a semantic shot packet. It should not be "objects": it should be a complete director brief that can compile down to the current engine recipe.

```json
{
  "version": 3,
  "intent": {
    "prompt": "a foggy mountain campsite beside a purple lake at dawn",
    "scene_class": "exterior",
    "scene_type": "mountain_lake_campsite",
    "must_read": ["campsite", "purple_lake", "mountains", "dawn", "fog"],
    "style": ["cinematic", "naturalistic", "dense"],
    "reject_if": ["interior_assets", "flat_empty_plane", "gray_lake"]
  },
  "shot": {
    "composition": "foreground_frame_midground_hero_background_horizon",
    "focal_subject": "tent_and_campfire",
    "camera": {"height_m": 1.45, "fov_deg": 55, "lens": "wide_cinematic"},
    "bands": ["foreground_silhouette", "midground_campsite", "lake", "ridge_horizon"]
  },
  "world": {
    "terrain": {
      "extent_m": 48,
      "base": "grass_rock_moss",
      "heightfield": {"enabled": true, "undulation": 0.45},
      "shoreline": {"shape": "curved_cove", "wet_edge_m": 2.0}
    },
    "waterbody": {
      "type": "lake",
      "bounds": "far_mid_band",
      "color_intent": "purple",
      "shallow": [0.45, 0.28, 0.78],
      "deep": [0.08, 0.03, 0.20],
      "roughness": 0.12,
      "reflection_weight": 0.35
    },
    "background": {
      "ridge_layers": [
        {"distance_m": 55, "height_m": 10, "color": [0.18, 0.20, 0.25]},
        {"distance_m": 85, "height_m": 18, "color": [0.10, 0.12, 0.18]}
      ]
    },
    "scatter_masks": {
      "tree_flanks": "left_right_background",
      "rocks": "shore_and_foreground",
      "grass": "near_camera_breakup",
      "clear": "camera_to_campfire_view_corridor"
    }
  },
  "generator_graph": [
    {"id": "terrain_massing", "kind": "heightfield", "inputs": ["shot", "world.terrain"], "outputs": ["terrain_height", "shore_mask"]},
    {"id": "ridge_backdrop", "kind": "procedural_mesh", "inputs": ["world.background"], "outputs": ["ridge_meshes"]},
    {"id": "tree_points", "kind": "scatter", "inputs": ["tree_flanks_mask"], "outputs": ["pine_instances"]},
    {"id": "camp_grammar", "kind": "shape_grammar", "inputs": ["setpieces"], "outputs": ["tent", "fire", "log_seating"]}
  ],
  "asset_prototypes": [
    {"id": "pine_tree", "query": "pine tree", "role": "tree", "quality": "scatter"},
    {"id": "hero_tent", "query": "open campsite tent", "role": "camp", "quality": "hero"},
    {"id": "shore_rock", "query": "mossy shoreline rock", "role": "rock", "quality": "scatter"}
  ],
  "instance_groups": [
    {"prototype": "pine_tree", "source": "pine_instances", "budget": {"min": 24, "max": 80}},
    {"prototype": "shore_rock", "source": "shore_mask", "budget": {"min": 18, "max": 60}}
  ],
  "setpieces": [
    {"type": "tent", "role": "hero", "placement": "midground_right", "scale_m": 2.8},
    {"type": "campfire", "role": "hero_light", "placement": "midground_center", "scale_m": 1.0},
    {"type": "logs", "role": "seating_and_leading_lines", "placement": "around_fire"}
  ],
  "dressing": {
    "density": "high",
    "families": ["pine_trees", "mossy_rocks", "wet_shore_stones", "grass_tufts", "fallen_logs"],
    "variation": {"scale": true, "yaw": true, "tint": true, "cluster": true}
  },
  "materials": {
    "water": {"absorption": 0.85, "fresnel": 0.28, "foam": 0.25},
    "shore": {"wetness": 0.55, "roughness": 0.38},
    "grass": {"moss_bias": 0.7, "saturation": 0.55},
    "rocks": {"roughness": 0.82, "moss": 0.35}
  },
  "lighting": {
    "time": "dawn",
    "sun": {"elevation_deg": 8, "azimuth_deg": 135, "color": [1.0, 0.55, 0.28], "intensity": 3.2},
    "fill": {"sky_ibl": "cool_low", "strength": 0.75},
    "rim_light": {"enabled": true, "target": "tent_trees"},
    "practicals": [{"type": "campfire_glow", "intensity": 5.5, "range_m": 7.5}]
  },
  "atmosphere": {
    "fog": {"density": 0.018, "start_m": 5.0, "height_falloff": 0.22},
    "aerial_perspective": {"background_desaturation": 0.45, "ridge_fade": 0.65},
    "particles": {"mist": 0.8, "embers": 0.3}
  },
  "post": {
    "exposure": 0.9,
    "contrast": 1.12,
    "saturation": 1.08,
    "bloom": 0.35,
    "vignette": 0.18
  },
  "quality": {
    "min_instances": 80,
    "budgets": {"hero_assets": 4, "scatter_instances": 80, "max_unique_meshes": 24},
    "must_have_pixel_color": {"region": "lake", "hue": "purple"},
    "known_bad_rejects": ["kitchenfridge", "flat_gray_lake", "empty_horizon"]
  }
}
```

## Why v2 Cannot Reach The Bar

- It has no first-class terrain, shoreline, ridge, waterbody, fog-volume, or biome layers.
- It treats mountains and lake shape as props or environment knobs, but those are scene structure.
- It has no shot grammar. Objects scatter into zones, but zones are not composition.
- It has no material director. Prompt color reaches the IR but may not survive shader, fog, IBL, exposure, or tone mapping.
- It has no fidelity budget. "26 objects valid" can still be sparse and toy-like.
- It has no semantic quality gate. `ridge -> kitchenfridge` passed because it was loadable and in-bounds.
- It has no prototype/instance split. Repeated scene detail should be instanced from prototypes with per-instance attributes, not authored as unrelated unique object records.

## Vertical Slice

The first vertical slice is one prompt only:

```text
a foggy mountain campsite beside a purple lake at dawn
```

The slice is complete only when the output has:

- Procedural background ridges visible behind the lake.
- A non-rectangular lake/shoreline or at least a visually shaped cove edge.
- Purple lake pixels verified by ROI and human review.
- A campsite focal cluster: tent, campfire, logs, local warm glow.
- Dense but controlled dressing: trees on flanks, rocks/grass/logs in believable masks.
- Dawn/fog lighting: warm low sun, cool fill, aerial perspective, fog falloff.
- A strict gate that rejects the current bad PNG and accepts the new one only with evidence.

## Phases

### Phase 0: Instruments and Known-Bad Gate

Create a quality gate before changing generation:

- fixture: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0.png`
- fixture IR: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0_ir.json`
- gate rejects indoor exterior assets, missing prompt-critical entities, gray lake ROI, sparse scene, and flat horizon
- output: `build/bin/logs/quality_gate_<name>.json`

### Phase 1: Director IR v3 Compiler Skeleton

Add v3 modules without removing v2:

- `tools/director_ir_v3.py`: typed schema, validation, examples
- `tools/scene_director.py`: prompt + catalog summary -> Director IR v3 using existing backend pattern
- `tools/scene_compiler.py`: Director IR v3 -> current v2 Scene IR
- `tools/director_quality.py`: semantic and visual gates
- `tools/asset_resolver.py`: extracted ladder from current `scene_gen.py`
- `tools/layout_solver.py`: extracted v2 solvers and validity checks
- `tools/scene_gen.py`: orchestrator; new optional v3 path, later promoted to default after the slice passes

### Phase 2: Procedural World Layer

Implement first-class world geometry:

- background ridge mesh or layered billboard/mesh silhouettes
- shaped lake/shoreline mask
- terrain height/normal breakup
- wet shore band
- scatter masks for trees, rocks, grass, logs

### Phase 3: Material and Lighting Director

Make prompt color and mood survive rendering:

- water optics: absorption, fresnel/reflection balance, roughness, body tint
- ground/shore material: wetness and roughness
- dawn rig: low warm sun, cool fill, local campfire practical, fog settings, grade
- render A/B harness to prove each knob changes final pixels

### Phase 4: Shot Grammar and Dressing

Replace free scatter with authored scene grammar:

- bands: foreground frame, midground hero, lake, ridge horizon
- hero cluster: tent/fire/logs
- side flanks: tree masses, not center blockers
- dressing density by masks with variation and collision rules

### Phase 5: Asset Fidelity Ladder

Audit and fill missing assets:

- hero: better tent, campfire, logs, camping props
- terrain: ridge/rock formations, moss, shore stones
- vegetation: higher-density pines, shrubs, grass clumps
- material: PBR textures for moss/rock/wet soil/water shore
- normalize tags, scale, bounds, role, biome, material class, quality tier

Required asset metadata:

- taxonomy: biome, scene_role, scale_class, placement_zone, support_surface, footprint_m, height_m, pivot_policy, up/forward_axis, collision_proxy, lod_chain, occlusion_class, hero/background/scatter, season, weathering, license/provenance, preview_capture.
- material contract: base color color space, normal convention, roughness/metallic/AO packing, height/displacement, opacity/alpha mode, emissive, wetness, snow/moss/dirt masks, tiling scale, texel density, fallback policy.

### Phase 6: Promote and Generalize

After the campsite slice passes:

- add lake, forest, beach, desert, garden grammars
- build a small benchmark of 12 high-fidelity prompts
- require every generated scene to pass semantic, material, color, composition, and human-gate review
- keep v2 as fallback until v3 passes the old battery plus new quality suite

## Verification

Machine gates:

- `scene_gen.py` result is valid and includes a v3 quality report
- no forbidden semantic roles in exterior scenes
- prompt-critical entities present in Director IR and compiled IR
- lake ROI hue/saturation matches purple intent
- object/dressing density above threshold with controlled collision
- horizon/ridge layer present for mountain prompt
- render logs confirm lighting/fog/water settings reached runtime
- v3 graph contains required layers and compiles deterministically from seeds
- repeated scatter uses prototypes/instance groups rather than unbounded unique asset records

Human gate:

- scene reads as a coherent cinematic campsite at a glance
- lighting and material response feel art-directed
- density and composition feel inspectable, not sparse

## Risks

- The renderer may not expose enough controls yet; if A/B probes cannot move final pixels, renderer API/shader work becomes the first implementation front.
- Asset quality may cap the result; procedural layers can fix terrain/composition but not hero prop fidelity.
- Vision critics may rubber-stamp. Known-bad fixtures and simple pixel/semantic metrics must gate before model judgment.
- Overfitting one prompt is possible. The first slice is allowed to be specific, but the architecture must compile other exterior prompts afterward.

## First Engineering Tasks

1. Build `tools/director_quality.py` and make the current bad PNG fail.
2. Draft `tools/director_ir.py` with the campsite example above as a schema fixture.
3. Implement a v3-to-v2 compiler that emits the current IR plus extra ignored fields.
4. Add one renderer-supported procedural background ridge and lake/shore geometry path.
5. Add water/lighting A/B probes and record pixel metrics.
6. Re-render the prompt and iterate against the gate.
