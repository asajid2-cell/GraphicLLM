# Scene Primitive And Asset Kit Sources

Status: working intake notes for model-authored final-art scenes.

## Immediate Engine-Native Kit

The current implementation uses compound primitives before broad asset intake:

- `home_kitchen_lantern`: enclosed kitchen, tile grid, cabinets, fridge, sink, backsplash, table, lantern, countertop clutter, lighting cues.
- `home_office_evening`: enclosed office, desk, monitor, keyboard, lamp, chair, window blinds, bookshelf, books.
- `school_classroom_day`: classroom shell, chalkboard, repeated desks/chairs, notebooks, window, ceiling light.
- `stadium_night_match`: turf field, markings, goals, bleachers, repeated seats, scoreboard, floodlights.

Reason: compound primitives are editable, cheap, deterministic, and can be validated for support/contact immediately. They are not the final art ceiling; they are the structural layer that lets later mesh assets land in coherent rooms.

## CC0 / Open Asset Candidates

### Poly Haven

Source: https://polyhaven.com/license

Use for naturalistic PBR props, scanned objects, HDRIs, and texture packs. Existing `assets/models/naturalistic_showcase/asset_manifest.json` already treats Poly Haven as the preferred CC0 source and records orientation, bounds, pivot policy, and texture binding.

Next useful categories:

- domestic props: cookware, tableware, lights, chairs, textiles
- material surfaces: tile, wood, concrete, painted wall, metal
- lighting environments: interiors, overcast skies, night HDRIs

Admission rule: do not add a Poly Haven model to runtime scenes until its `asset_manifest.json` entry includes license, source URL, runtime glTF path, pivot policy, bounds, floor/contact policy, and texture status.

### Kenney

Sources:

- https://kenney.nl/support
- https://kenney.nl/assets/furniture-kit

Use for low-poly modular props and readable room-building kits. Kenney is valuable for breadth: furniture, food, city/suburban, factory, sports, school-like props, and modular architecture. The visual style is simplified, so Kenney assets should be used as structural/detail primitives unless the whole scene intentionally uses that style.

Next useful kits:

- Furniture Kit: chairs, shelves, tables, cabinets, sofas
- Food Kit: kitchen and table clutter
- City/Suburban Kit: house and street exteriors
- Factory Kit: industrial interiors and school/workshop props

Admission rule: prefer GLB/glTF variants when available. If a kit ships OBJ/FBX only, convert through a controlled import step and write a manifest entry for every admitted runtime asset.

### Khronos glTF Sample Assets

Sources:

- https://github.com/KhronosGroup/glTF-Sample-Assets
- https://github.khronos.org/glTF-Assets/

Use as renderer correctness/reference assets, not broad art content. These are useful for validating material features, glTF extensions, texture formats, animation/skinning, and loader behavior. Licenses vary per model, so every candidate must be checked individually before ingestion.

Admission rule: only import models whose license is compatible with the project and whose feature set Cortex actually supports. Keep unsupported extension tests separate from final-art scenes.

## What This Means For Authoring

The durable architecture is two-layered:

1. Compound primitive kits build the room, support surfaces, scale cues, and lighting structure.
2. Admitted CC0 meshes replace ugly primitive placeholders once their orientation, scale, contact, and material contracts are known.

This avoids the old failure mode where one nice mesh is placed into a bad blockout and still looks wrong. The scene graph must know what each object is for before better models can improve the image.

## Admitted Kenney Furniture Kit

Runtime path: `assets/models/kenney_furniture_kit/`

Manifest: `assets/models/kenney_furniture_kit/asset_manifest.json`

Importer: `tools/import_kenney_furniture_kit.py`

Admitted assets: all `140` OBJ files from the downloaded Kenney Furniture Kit.
The importer now discovers every OBJ in
`assets/source_assets/kenney/furniture-kit/Models/OBJ format/`, converts it to
indexed Cortex-readable glTF, and writes manifest tags/budget classes. Curated
overrides are still used for known kitchen/office assets, but newly admitted
assets such as `bench`, `benchCushion`, `chairModernCushion`,
`loungeSofaLong`, `loungeDesignSofa`, `tableCoffeeGlass`, `lampWall`,
`speaker`, `televisionModern`, `wall`, `floorFull`, `stairs`, `rugRound`,
`pillow`, and `stoolBar` are available to the model-authored arranger.

The importer converts the Kenney OBJ files to Cortex-readable `.gltf` files so the spatial validator can read POSITION accessor bounds. This is preferred over using opaque `.glb` files for the current loop because contact validation remains cheap and inspectable.

Current use:

- `home_kitchen_lantern` now replaces primitive fridge/sink/stove/toaster placeholders and several countertop/hood props with admitted Kenney meshes.
- `home_office_evening` now replaces primitive desk/monitor/keyboard/mouse/chair/bookcase/books/lamp placeholders and adds laptop, speakers, radio, ceiling light/fan, floor lamp, and plant meshes.
- `red_light_room` now layers admitted sofa, coffee table, floor lamp, and TV/screen meshes over the primitive room blockout.
- `neon_streamer_concert` now layers admitted desk, TV/screen frame, speaker, and sampled chair meshes over the primitive auditorium blockout.

Known limitation:

The first office capture proved runtime mesh loading but framed the desk too
closely and leaked external IBL/background when the camera sat outside the room.
Current interior captures use a camera-inside-room validation rule and
clean-capture mode. The red room mesh pass is a visible improvement; the
concert mesh pass proves asset availability, but the stage composition still
needs mesh-aware rules so physical screen frames do not fight emissive display
planes. The next improvement should be stronger material/texture admission and
composition constraints for layered mesh/display pairs.
