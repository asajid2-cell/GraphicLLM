# AAA Scene Generation Fan-Out

## Working Question

How do we turn the current prompt-to-scene generator into a real AA/AAA-quality scene director for detailed scenes with manipulated lighting, shading, materials, terrain, camera, composition, and verification?

The answer must be grounded in:

- the actual Cortex engine capabilities,
- the current Python generator pipeline,
- the asset/material pipeline,
- objective quality gates,
- production procedural-scene practice.

## Lane Briefs

| Lane | Owner | Question | Expected Output | Status |
|---|---|---|---|---|
| A Renderer capability audit | agent `019f26d3-e777-7da1-9cb6-3ce929646b6d` | Which renderer controls already exist, what is missing, and what Director IR fields should expose them? | File/line-backed report of lighting, fog, water, materials, camera, particles, post, terrain, and gaps | complete |
| B Generator architecture audit | agent `019f26d4-0ecd-7e22-872b-050b89cc1f95` | Where should Director IR v3 fit in the current prompt-to-render pipeline? | Pipeline boundary map, extension points, migration plan, first vertical slice | complete |
| C Asset/material pipeline audit | agent `019f26d4-3864-7533-82ed-c97a9dc03bd5` | What asset/material system is needed for AAA and what does the current ladder lack? | Catalog counts, missing categories, material limitations, taxonomy priorities | complete |
| D Quality/evaluation audit | agent `019f26d4-6153-7882-9164-de499bb874cb` | How should we verify AAA scene generation and why did current gates pass a bad scene? | Gate design, fixture plan, metrics, human-gate boundaries | running |
| E External production-practice research | agent `019f26d7-1cdb-70a1-927f-333bd3e48d34` | What do Unreal/SideFX/OpenUSD patterns imply for prompt-to-scene architecture? | Cited principles and Director IR implications | complete |

## Integrated Findings So Far

### Lane A: Renderer Capability

Known controls already exist:

- Renderer scene profiles bundle environment, lighting, reflections, probes, temporal, post, material palette, and water.
- Renderer API covers exposure, auto-exposure, bloom, IBL/environment/background, color/tone grade, cinematic post, SSAO/SSR, fog, water, god rays, ray tracing, sun, vegetation/wind.
- ECS lights include directional/point/spot/rect area semantics; particles include mist, embers, steam, smoke, rain/snow; material components include rich PBR fields, transmission, IOR, clearcoat, sheen, anisotropy, wetness.
- Volumetric froxel shader has density, height falloff, anisotropy, scatter, lights, shafts, and local shadows.
- Terrain/mesh generation and river/lake/waterfall APIs exist but are not wired into the generative IR path.

Renderer gaps:

- Director-facing command layer is much narrower than renderer capability.
- Fog/volumetric extra params are shader-side but not clean high-level Director fields.
- Clouds/weather exist but are weakly integrated into runtime authoring.
- Water lacks shoreline masks, flow maps, caustic controls, foam masks, bathymetry, and local wave zones.
- Multi-probe reflection capture is underpowered.

Implication: v3 can initially compile to the current v2 IR, but the engine bridge must be widened quickly so Director IR can drive renderer controls directly.

### Lane B: Generator Architecture

Best fit:

```text
prompt -> Director IR v3 -> scene compiler -> existing v2 Scene IR -> render_ir.ps1 -> C++ generative recipe
```

Do not push v3 straight into the C++ engine first. Keep `CORTEX_SCENE_IR_JSON` as the runtime contract while v3 matures.

Proposed files:

- `tools/director_ir_v3.py`
- `tools/scene_director.py`
- `tools/scene_compiler.py`
- `tools/asset_resolver.py`
- `tools/layout_solver.py`
- `tools/scene_gen.py` as orchestrator

V2 blockers:

- exterior schema is environment scalars plus flat object groups,
- solver is 2D zone scatter,
- engine exterior terrain is flat land plus tilted seabed/water plane,
- camera is fixed establishing shot plus critique deltas,
- procgen only makes rock-like meshes.

### Lane C: Asset/Material Pipeline

Runtime catalog:

- 511 loadable assets.
- Enough for blockout campsite: trees, cliffs/rocks, tent, campfire, canoe.
- Not enough for AAA: no real mountain-range/backdrop kit, shoreline ecology, docks/piers, camping clutter, dense ground scatter, terrain decals, seasonal variants, LODs, collision proxies, or biome material sets.

Material bottleneck:

- Runtime material system is rich, but glTF ingest mostly reads core PBR texture paths and collapses merged primitives toward one material.
- Existing registry says 0 AAA-ready assets because of missing complete PBR sets, LOD chains, collision proxies, and preview evidence.

Asset v3 requirements:

- taxonomy: biome, scene_role, scale_class, placement_zone, support_surface, footprint/height, pivot policy, axes, collision, LOD, occlusion class, hero/background/scatter, season, weathering, license, preview.
- material contract: base color color space, normal convention, ORM packing, height/displacement, opacity, emissive, wetness, snow/moss/dirt masks, tiling scale, texel density.

Priorities:

1. Campsite-lake-mountain kit.
2. Registry-driven normalized assets.
3. Preserve per-primitive/material slots or pre-split assets.
4. Procedural generators for terrain/shore/scatter, not only rocks.

### Lane D: Quality / Evaluation

Current gates are permissive:

- `scene_gen.py` exits success based on `validity_check(ir)`, not render quality.
- exterior validity only checks bounds, overlap, vegetation-in-water, and center corridor.
- `scene_battery.py` counts `reframe` as acceptable and only needs 75% intent pass.
- the resolver accepts unsafe exact/substring matches; verified current bug: `misty mountain ridge => kitchenfridge appliance keyword(2)`.
- `render_ir.ps1` returns a PNG path but does not preserve or consume frame reports for generation quality.

Quality gate requirements:

- semantic object/domain allowlists: exterior campsite/lake/mountain forbids appliance/kitchen/bathroom/office assets unless explicitly prompted.
- prompt-critical entities: campsite = tent + fire/camp dressing; mountain = ridge/background terrain layer; lake = water body; purple = rendered water hue; dawn/foggy = low sun + fog telemetry.
- color ROI: current bad render water samples as gray-green, not purple. Require red/blue dominance, green suppression, and minimum saturation in lake ROI or water mask.
- composition/density: foreground/midground/background occupancy, focal subject screen coverage, edge/local contrast density, no giant empty center unless intended.
- renderer telemetry: require water count/draws, mesh/draw count, no visual health warnings, and expected environment state.

Proposed artifacts:

- `tools/scene_quality_gate.py`
- `tools/run_scene_quality_gate.ps1`
- `build/bin/logs/scene_quality/<slug>/<run_id>/prompt.txt`
- `build/bin/logs/scene_quality/<slug>/<run_id>/scene_ir.json`
- `build/bin/logs/scene_quality/<slug>/<run_id>/render.png`
- `build/bin/logs/scene_quality/<slug>/<run_id>/frame_report_last.json`
- `build/bin/logs/scene_quality/<slug>/<run_id>/quality_metrics.json`
- `build/bin/logs/scene_quality/<slug>/<run_id>/quality_gate_report.json`
- `build/bin/logs/scene_quality/<slug>/<run_id>/roi_water.png`

Known-bad fixture:

- prompt: `a foggy mountain campsite beside a purple lake at dawn`
- image: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0.png`
- IR: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0_ir.json`
- expected failures: `forbidden_asset_class`, `resolver_regression`, `missing_prompt_entity`, `purple_water_roi_fail`, `focal_visibility_fail`.

### Lane E: External Production Practice

All sources are primary/authoritative: Epic/Unreal, SideFX, and OpenUSD.

Production principles:

- Treat prompt-to-scene as a graph/pipeline, not a flat spawn list.
- Make spatial intent first-class: volumes, splines, surfaces, heightfields, point clouds, masks.
- Separate high-level biome/layout passes from local detail passes.
- Keep generation data-driven and extensible through graph sections, data assets, reusable subgraphs, and feedback loops.
- Use grammar for structured man-made content.
- Terrain should be staged: massing, seeding, remapping/elevation, upsampling, shaping, erosion, then shading/scattering.
- Scatter should emit points with attributes, then instance prototypes; do not directly emit unique objects.
- Use composition, references, payloads, variants, and instancing for scene assembly.
- Lighting/look should be declarative scene state: sky, GI/reflections, fog, post, exposure, color.
- Budget for instancing, streaming, and material reuse at the IR level.

Primary sources:

- Epic PCG overview: https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-overview
- Epic PCG data types: https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-framework-data-types-reference-in-unreal-engine
- Epic PCG Biome Core: https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-pcg-biome-core-and-sample-plugins-overview-guide-in-unreal-engine
- Epic PCG generation modes: https://dev.epicgames.com/documentation/unreal-engine/using-pcg-generation-modes-in-unreal-engine
- Epic runtime hierarchical generation: https://dev.epicgames.com/documentation/unreal-engine/runtime-hierarchical-generation
- Epic Shape Grammar with PCG: https://dev.epicgames.com/documentation/unreal-engine/using-shape-grammar-with-pcg-in-unreal-engine
- SideFX terrain workflow: https://www.sidefx.com/docs/houdini/model/terrain_workflow.html
- SideFX HeightField Scatter: https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_scatter.html
- SideFX Scatter and Align: https://www.sidefx.com/docs/houdini/nodes/sop/scatteralign.html
- SideFX scattering attributes: https://www.sidefx.com/docs/houdini/heightfields/scatterattribs.html
- OpenUSD composition arcs: https://openusd.org/release/glossary.html#composition-arcs
- OpenUSD performance guide: https://openusd.org/release/maxperf.html
- OpenUSD scenegraph instancing: https://openusd.org/dev/api/_usd__page__scenegraph_instancing.html
- Epic Lumen: https://dev.epicgames.com/documentation/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine
- Epic Sky Atmosphere: https://dev.epicgames.com/documentation/unreal-engine/sky-atmosphere-component-in-unreal-engine
- Epic Volumetric Fog: https://dev.epicgames.com/documentation/unreal-engine/volumetric-fog-in-unreal-engine
- Epic Post Process Effects: https://dev.epicgames.com/documentation/unreal-engine/post-process-effects-in-unreal-engine
- Epic Instanced Static Meshes: https://dev.epicgames.com/documentation/unreal-engine/instanced-static-mesh-component-in-unreal-engine
- Epic Material Instances: https://dev.epicgames.com/documentation/unreal-engine/instanced-materials-in-unreal-engine
- Epic Runtime Virtual Texturing: https://dev.epicgames.com/documentation/unreal-engine/runtimevirtual-texturing-quick-start-in-unreal-engine
- Epic Nanite: https://dev.epicgames.com/documentation/unreal-engine/nanite-virtualized-geometry-in-unreal-engine

Director IR implications:

- `scene_layers`: terrain, biome, authored overrides, generated detail, lighting/look.
- `spatial_regions`: volumes, splines, surfaces, heightfields, masks, biome weights.
- `generator_graph`: passes, dependencies, seeds, reusable subgraphs.
- `scale_grids`: landmarks, mid-scale assets, small debris/grass, streaming radius.
- `terrain_pipeline`: massing, elevation, erosion, masks, material layers.
- `scatter_rules`: density, mask, spacing, orientation, variants.
- `shape_grammar`: modules, grammar strings, spline/axis constraints.
- `asset_prototypes` and `instance_groups`: prototype reuse plus transforms/attributes.
- `lighting_look`: sky, fog, post, exposure, grade, time of day.
- `budgets`: instances, draw calls, memory, streaming buckets.

## Decisions Locked In

- Current `scene_gen.py` is not the final architecture.
- v3 starts in Python and compiles to current v2 IR until engine features require new C++ fields.
- First vertical slice remains the campsite/purple lake/mountain/fog/dawn prompt.
- The first implementation step is quality/instrumentation plus schema, not random visual tweaking.

## Open Inputs

- Lane D: exact quality gate design.
- Lane E: cited external architecture principles.
- None.

## Next Integration Step

1. update `AAA_SCENE_GEN_PLAN.md`,
2. update `CAMPAIGN.md`,
3. create the first implementation backlog in dependency order.
