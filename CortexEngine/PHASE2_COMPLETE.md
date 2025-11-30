# Phase 2 - The Architect (Complete)

Phase 2 integrates Llama.cpp into the engine and enables natural-language control of the scene.
This document summarizes the capabilities, the command format, and how to use the system, and how
the Architect now ties into the curated hero scene and presentation layer.

---

## What Phase 2 Adds

- In-process LLM service based on Llama.cpp.
- JSON-based command language for scene manipulation.
- Thread-safe command queue between the LLM and the renderer.
- Text input overlay for typing commands while the scene is running.
- Regression tests to validate command handling.
- Tight integration with the hero "Dragon Over Water Studio" scene, including material presets and
  lighting rigs that the Architect can address by name.

---

## Natural-Language Scene Control

Examples of supported prompts:

- "Add a red cube at position 2, 1, 0."
- "Make the cube blue and move it up."
- "Add a shiny metal sphere next to the cube."
- "Remove the red cube."
- "Move the camera to 5, 3, 5."
- "Make the dragon brushed metal and slightly rougher."
- "Set the scene to a sunset environment with stronger reflections."

The LLM produces JSON commands; the engine parses those commands and then applies them to the ECS.
Because the hero scene is tagged with semantic names and presets (for example `MetalDragon`,
`PoolRim`, `wood_floor`, `chrome`), the Architect can perform meaningful edits in place rather
than only spawning new primitives.

---

## Command System

### Supported command types

- `add_entity` - spawn a primitive at a given transform with optional color and material.
- `remove_entity` - delete an entity by name.
- `modify_transform` - update position, rotation, or scale.
- `modify_material` - update color, metallic, roughness, AO, or apply a named material preset
  such as `chrome`, `brushed_metal`, `plastic`, `ceramic`, `skin`, `water`, `concrete`, `wood`.
- `modify_camera` - move the main camera or change FOV.

### Example JSON

```json
{
  "commands": [
    {
      "type": "add_entity",
      "entity_type": "sphere",
      "name": "MetalSphere",
      "position": [3, 1, 0],
      "scale": [0.8, 0.8, 0.8],
      "color": [0.7, 0.7, 0.7, 1.0]
    },
    {
      "type": "modify_material",
      "target": "MetalSphere",
      "metallic": 0.9,
      "roughness": 0.1
    }
  ]
}
```

The grammar is intentionally small and explicit so that commands can be validated and extended
safely. Additional high-level commands (for example `modify_renderer` and `add_light`) allow the
Architect to tune exposure, enable or disable ray tracing, switch environments (studio, sunset,
night), and adjust the hero lighting rig.

---

## How to Use The Architect

### Controls

- **T** - enter or exit text input mode.
- **Enter** - submit the current line to the Architect.
- **Esc** - cancel text input; from normal mode, closes the application.
- **Backspace** - delete the last character in the input line.

While the Architect processes a command, the renderer continues to run at full frame rate. When the
LLM completes, parsed commands are enqueued and applied on the next frame.

The hero scene and presentation features make these edits easy to see:

- A HUD overlay shows the active debug view, RTX state, environment name, AA mode, SSR/SSAO, and
  fog toggles so renderer changes are always visible.
- When an entity is selected, the HUD also displays its material preset and numeric PBR values
  (albedo, metallic, roughness, AO), which makes LLM-driven material tweaks feel precise rather
  than opaque.

### Running in mock mode

The engine can run in a mock mode (no model required) where the Architect
returns pre-defined JSON snippets. This is useful for development or when
models are not available:

```powershell
.\run.ps1
```

In mock mode, regression tests are also available to exercise the command
handler without relying on the model.

---

## Implementation Notes

- LLM inference runs on a dedicated worker thread owned by `LLMService`.
- Token-to-text conversion uses the correct buffer sizing logic so that all generated tokens are
  preserved.
- The command queue is the only synchronization point between threads; ECS access is always
  performed on the main thread.
- Renderer and ECS access from LLM commands are confined to the main thread; background work only
  prepares JSON and texture data.

On the rendering side, Phase 2 also tightens core stability so the hero scene feels solid even
before enabling ray tracing:

- Cascaded shadows clamp invalid light-space coordinates and use softer cascade bias scaling to
  avoid random striping in the shadow debug views.
- Temporal anti-aliasing now resolves in a dedicated HDR pass before SSR, bloom, and tone mapping,
  using depth/normal-aware neighborhood clamping and motion-dependent blending so silhouettes
  (for example, the dragon against the floor) remain stable while the camera moves.
- A dedicated TAA debug view reports per-pixel history weight in that HDR resolve, making it
  straightforward to tune jitter amplitude and blend factors while watching how much history is
  actually being accumulated.
- The main scene can be rendered at a supersampled internal resolution (scaled HDR and depth
  targets), then downsampled in post-process; this further reduces shimmer and keeps TAA and
  SSR operating on higher-quality inputs without changing the window size.

Phase 2 completes the Architect layer and anchors it in a portfolio-ready presentation:

- A curated hero scene ("Dragon Over Water Studio") with tuned lighting, water, and materials.
- A hero visual baseline (studio environment, TAA, SSR + SSAO) that can be re-applied at any time.
- Auto-demo and camera bookmarks that make it easy to showcase the engine without manual setup.

This provides a clean foundation for future extensions such as richer command sets, multi-step
tools, and tighter integration with the diffusion pipeline.
