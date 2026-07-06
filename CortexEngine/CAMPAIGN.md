> [!ARCHIVED 2026-07-06] STALE - do not read for state. State = `CURRENT.md`; strategy = `PLAN.md`; queue = `QUEUE.md`; loop contract = `LOOPS.md`.

# Campaign: AAA Prompt-To-Scene Fidelity

## Win Condition
The goal is not "make this one render less wrong." The goal is to turn prompt-to-scene generation into a real scene director that can produce dense, art-directed, high-fidelity scenes with controlled lighting, shading, materials, terrain, atmosphere, camera, and composition.

Given `python tools/scene_gen.py "a foggy mountain campsite beside a purple lake at dawn"`, the output must be a high-fidelity campsite scene that is visibly and semantically correct:

- A purple lake reads purple in the final PNG, not gray.
- A mountain/ridge silhouette exists and no indoor assets appear in an exterior scene.
- The campsite has a deliberate AAA-style shot design: foreground silhouettes, midground focal campsite, background ridge/lake horizon, controlled negative space, and no random catalog clutter.
- Dawn/fog are visually authored: warm low sun, rim light, atmospheric aerial perspective, volumetric/fog layering, wet/specular highlights, and a grounded sky/lighting grade.
- Materials and shading are authored per surface: water absorption/reflection, wet shore, moss/grass, rock roughness, tree color, tent fabric, fire/emissive glow if present.
- The output is dense enough to inspect: terrain breakup, small dressing, believable scale, non-flat horizon, and no toy-like empty plane.
- The verifier rejects the current bad output and records why.

Human gate: whether the final scene feels "AA/AAA enough." Automated gates should catch objective misses before the human sees it, but the final bar is visual quality, not JSON validity.

## Constraints & Anti-goals

- Preserve the existing prompt-to-render command and backend flags.
- Do not regress current interior validity or the basic exterior battery.
- Do not use missing/placeholder assets to fake success.
- Keep GPU stability fixes from commits `e401361` and `00b48f4`.
- No behavior edits before recon evidence identifies the failing layer.
- Do not confine the solution to the current 513-asset catalog. Existing assets are inputs, not the target architecture.
- Do not accept "it matches the prompt" if the scene still looks like sparse low-poly props on a plane.

## Terrain Map

Known true:

- Current render artifact: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0.png`.
- Current IR: `build/bin/logs/gen_a_foggy_mountain_campsite_beside_0_ir.json`.
- The final IR contains two `kitchenfridge` objects where "misty mountain ridge" was resolved.
- The asset catalog has no `mount*`, `hill*`, or `terrain*` assets. `ridge` substring matches `kitchenfridge` and bridge assets.
- Water IR is purple-ish (`shallow=[0.3525,0.36,0.536]`, `deep=[0.0825,0.1225,0.204]`), but sampled final pixels in the lake band average near gray/green: `(134,143,137)` sRGB.
- Existing validity checks only prove spatial/loadability constraints. They do not check semantic asset class, prompt-critical object presence, color-readability, or composition quality.
- The old battery report counted many `verdict: reframe` outputs as success; it was a validity/robustness gate, not an AAA quality gate.
- Current `scene_gen.py` asks an LLM for an object list plus broad environment knobs. It does not compile a shot from art-direction layers.
- Current exterior representation has no first-class terrain forms, no biome dressing system, no material palette compiler, no lighting rig presets, and no scene-density budget.

Known false:

- The renderer is not simply failing to receive water color. The IR carries a purple water color.
- The mountain failure is not a missing single catalog synonym. There is no mountain/ridge terrain layer in the asset corpus.
- A better resolver alone cannot reach the quality bar. It prevents nonsense, but it does not create art direction, geometry density, lighting, or material fidelity.

Unknown:

- Exact water washout cause: fog/IBL/specular reflection/tone grade/water shader weighting need an A/B probe.
- Whether generated/fetched terrain assets are enough, or whether background ridges should be procedural engine geometry.
- Best automated quality metric for "AAA enough" without letting the critic rubber-stamp weak scenes.
- How much of the renderer already supports the needed high-fidelity controls versus how much must be added as new scene/renderer APIs.
- Whether the shortest path is to extend the current IR or introduce a v3 "Director IR" that compiles down to the current engine recipe.

## Solved Ground

| What | Evidence | Date |
|---|---|---|
| BYOK/Claude/Codex backend selection works | Commit `e401361`; successful compose with `codex ok` | 2026-07-03 |
| Full-quality render pacing and background scheduling exist | Commit `00b48f4`; logs showed GPU scheduling and frame pacing active | 2026-07-03 |
| Current bad output is valid by the old checker | User run: `VALID`, no dropped objects | 2026-07-03 |
| Fan-out planning completed | `docs/AAA_SCENE_GEN_FANOUT.md`, `docs/AAA_SCENE_GEN_PLAN.md` | 2026-07-03 |
| Director IR v3 is the default exterior path | `tools/scene_gen.py`; campsite/alpine/desert renders use `backend=director_v3` | 2026-07-03 |
| Procedural world layers render real ridges | Runtime log: `generative_exterior: created 2 procedural ridge layer(s)` | 2026-07-03 |
| Cabin prompts no longer fake cabins with tents | `environment.structures` + runtime log: `created 1 procedural cabin structure(s)` | 2026-07-03 |
| Objective gate catches black/underlit renders and mood/color misses | Alpine black artifact failed `render_blank_or_underlit`; warm alpine failed `moonlight_render_coolness_fail` | 2026-07-03 |

## Approach Tree

| # | Approach class | Prediction | Cheapest probe | Kill criteria | Status |
|---|---|---|---|---|---|
| 1 | Director IR v3 / scene compiler | AAA quality requires a layered director format: intent, biome, terrain, waterbody, hero set, dressing, materials, lighting, camera, post grade. The LLM should author intent; deterministic compilers should build the shot. | Draft v3 schema and compile the campsite prompt into a complete non-rendered scene packet with no asset IDs except final resolved leaves | Dead if v3 cannot represent current interiors/exteriors or produces no clearer controls than v2 | live |
| 2 | Procedural world layers | Catalog props cannot create landscapes. Terrain, ridges, shorelines, fog volumes, waterbody bounds, ground breakup, and vegetation scatter need first-class procedural layers. | Add one prototype layer: background ridges + shaped lake shoreline + terrain dressing mask for the campsite prompt | Dead if generated layers look worse than current flat plane or destabilize render/camera | live |
| 3 | Material and lighting director | The visual bar depends on art-directed surface response and lighting, not only object choice. Prompt color/mood must compile into water optics, ground wetness, sky/IBL, sun/rim/fill, fog, bloom, grade. | Build an A/B render harness for the same IR that sweeps water/fog/IBL/exposure and measures purple-readability plus manual image review | Dead if controls cannot visibly change the target surfaces, meaning renderer API/shader work comes first | live |
| 4 | Asset acquisition and fidelity ladder | AAA density needs many more relevant assets, normalized materials, LOD scale, and semantic tags. The 513-asset catalog is too small and under-tagged. | Build an asset audit for the prompt: missing hero, terrain, vegetation, dressing, material categories; produce required asset list and available substitutes | Dead if missing assets are not the blocker after procedural layers and material director improve quality | live |
| 5 | Shot grammar and camera composer | Object bags produce empty scenes. Each scene type needs authored grammar: hero/focal area, leading lines, occluders, foreground framing, background horizon, density bands. | Implement a campsite-lake grammar in data/code and compare against current random scatter using the same prompt | Dead if grammar makes scenes less flexible or fails human-gate composition review | live |
| 6 | Semantic resolver and ontology gate | This is hygiene, not the main win. It prevents nonsense like `ridge -> kitchenfridge` and keeps asset domains clean. | Unit probe `resolve_query("misty mountain ridge")` and a validity gate that rejects appliance/path roles in exterior landscape queries | Dead if `kitchenfridge` still appears or if valid exterior prompts lose needed assets | live |
| 7 | Quality gate / judge battery | The old gate is permissive. The new gate must reject sparse toy scenes and require art-direction evidence. | Build `scene_quality_gate.py` with prompt-critical checks, ROI color checks, density/composition metrics, and vision review; current PNG is a known-bad fixture | Dead if gate passes the current bad image or blocks good scenes without actionable reasons | live |

## Fronts

| Front | Mechanism | State | Last advance |
|---|---|---|---|
| Failure attribution | self | Recon complete enough for plan | Current image + IR + catalog + resolver + renderer reads |
| Director IR v3 | self | Green for exterior default path | Campsite, alpine, desert all generated through v3 |
| Procedural world layers | self/loops | Green for ridge/cabin/water slice | Runtime ridge and cabin logs, PNG gates |
| Material + lighting director | self | Green for water color + moonlight slice | Purple/turquoise water ROI and moonlight coolness gates |
| Asset fidelity ladder | self | Not started | Needs missing-asset audit |
| Shot grammar | self/loops | Green for campsite/cabin/desert slice | Hero clusters, structures, water/ridge bands |
| Resolver/ontology | self | Green for known bad | `kitchenfridge` rejected in exterior quality gate |
| Quality verifier | self/vision | Green for objective gates | Known-bad, campsite, alpine, desert, kitchen frame smoke |

## Implementation Backlog

Dependency order:

1. **Known-bad quality gate.**
   - Add `tools/scene_quality_gate.py`.
   - Use current bad PNG + IR as a negative fixture.
   - Fail on `kitchenfridge`, missing ridge layer, gray lake ROI, unreadable campsite focal subject, and weak density/composition.
   - This is first because the old gate is known to lie.

2. **Director IR v3 schema.**
   - Add `tools/director_ir_v3.py`.
   - Include prompt intent, scene layers, spatial regions, generator graph, scale grids, terrain pipeline, scatter rules, shape grammar, asset prototypes, instance groups, lighting/look, materials, budgets, and quality constraints.
   - Include the campsite-lake-mountain example as a schema fixture.

3. **Compiler skeleton, v3 to current v2 IR.**
   - Add `tools/scene_director.py` and `tools/scene_compiler.py`.
   - Keep `CORTEX_SCENE_IR_JSON` unchanged.
   - Compile v3 to current exterior IR first, even if some v3 fields are not yet renderable.

4. **Semantic resolver/ontology guard.**
   - Extract or wrap current resolver.
   - Prevent cross-domain matches such as `ridge -> kitchenfridge`.
   - For exterior landscape prompts, forbid interior roles unless explicitly requested.

5. **Campsite-lake shot grammar.**
   - Encode bands: foreground frame, midground campsite, lake, ridge horizon.
   - Encode hero set: tent, campfire/practical glow, logs, shore rocks, tree flanks.
   - Emit prototypes and instance groups in v3; lower to v2 entries for now.

6. **Procedural world layer.**
   - Add ridge/backdrop and shaped shoreline/terrain breakup.
   - Prefer C++ support once the compiler proves the exact needed fields.
   - Temporary v2 fallback can use generated meshes/assets only if the quality gate labels it as provisional.

7. **Material/lighting director.**
   - Expose renderer controls that already exist but are not reachable from the current command bridge.
   - First controls: water absorption/fresnel/reflection balance, fog params, IBL/background exposure/rotation, post grade, bloom, campfire practical.
   - Add A/B probe harness that proves each knob moves final pixels.

8. **Asset/material registry upgrade.**
   - Add taxonomy and material metadata.
   - Prioritize campsite-lake-mountain kit: mountain backdrops, cliff/talus, shoreline rocks, reeds, camp clutter, tent/fire/log upgrades, ground scatter.
   - Fix multi-material ingest or pre-splitting so high-fidelity assets keep material slots.

9. **Promote v3 to default only after the slice passes.**
   - Keep v2 fallback.
   - Run old battery plus new quality suite.
   - Human-gate the final campsite render.

## Beat Log

2026-07-03:

- Reconstructed inherited state from archive.
- Inspected the user's failed output image.
- Read current IR and found semantic failure: `kitchenfridge` objects in exterior scene.
- Confirmed catalog lacks true mountain/hill/terrain assets.
- Confirmed water color is present in IR but not visible in final pixels.
- Wrote approach tree. Highest-leverage next action: implement a failing quality gate for this exact prompt/output before changing generation.
- User corrected the frame: fixing current constraints is insufficient; the target is real detailed AAA scene generation with manipulated lighting, shading, materials, terrain, camera, and composition. Updated campaign to treat current `scene_gen.py` as a prototype and prioritize a Director IR / scene compiler architecture.
- Ran fan-out lanes:
  - Renderer: engine has many high-fidelity controls, but the generator/command bridge exposes too little.
  - Generator: v3 should sit before current v2 IR and compile to existing `CORTEX_SCENE_IR_JSON` first.
  - Asset/material: catalog is enough for blockout, not AAA; material normalization and metadata are a major cap.
  - Quality: old gate only proves renderability; current bad PNG must become a negative fixture.
  - External: production systems use graphs, spatial masks, heightfields, scatter attributes, prototypes/instances, lighting/look volumes, and budgets.
- Implemented Director IR v3 default exterior path, v3-to-current compiler, quality gate, authored water shader path, procedural ridge meshes, procedural cabin structures, and rendered moonlight/canyon/turquoise objective checks.
- Verified:
  - known-bad campsite still rejected for wrong asset class, missing ridge, weak purple water, and poor focal cluster.
  - fixed campsite passes with purple ROI `purple_fraction=0.8846`.
  - novel alpine cabin moonlight passes with `cool_fraction=0.4842`, `nonblack_fraction=0.9999`, runtime cabin/ridge creation logs.
  - novel desert canyon turquoise river passes with `turquoise_fraction=0.381`, `avg_saturation=0.2759`.
  - legacy kitchen smoke render passes as interior `VALID` and nonblank frame.

## Learnings

- "Loadable and non-overlapping" is not scene quality.
- Free-text substring matching across the entire asset ID corpus is unsafe for exterior semantic queries.
- Landscape concepts such as mountains, ridges, horizons, and shorelines are scene geometry, not loose props.
- Explicit color intent must be verified in the rendered image, not only in IR.
- AAA scene generation needs a director stack, not an LLM object list: intent -> shot grammar -> procedural world -> asset/material resolution -> lighting/camera/post -> verifier.
- IR-only gates are not enough; every rendered prompt needs a nonblank/inspectable frame gate.
- Transparent material paths can destabilize quick smoke renders; procedural emissive cabin windows stay opaque unless a separate transparency gate is added.

## BLOCKED / Decisions needed

None. Objective gates are green. HUMAN-GATE remains for whether the rendered images meet the user's AA/AAA bar.
