# Phase 2 - The Architect (Complete)

Phase 2 integrates Llama.cpp into the engine and enables natural-language control of the scene.
This document summarizes the capabilities, the command format, and how to use the system.

---

## What Phase 2 Adds

- In-process LLM service based on Llama.cpp.
- JSON-based command language for scene manipulation.
- Thread-safe command queue between the LLM and the renderer.
- Text input overlay for typing commands while the scene is running.
- Regression tests to validate command handling.

---

## Natural-Language Scene Control

Examples of supported prompts:

- "Add a red cube at position 2, 1, 0."
- "Make the cube blue and move it up."
- "Add a shiny metal sphere next to the cube."
- "Remove the red cube."
- "Move the camera to 5, 3, 5."

The LLM produces JSON commands; the engine parses those commands and then
applies them to the ECS.

---

## Command System

### Supported command types

- `add_entity` - spawn a primitive at a given transform with optional color and material.
- `remove_entity` - delete an entity by name.
- `modify_transform` - update position, rotation, or scale.
- `modify_material` - update color, metallic, roughness, AO.
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

The grammar is intentionally small and explicit so that commands can be
validated and extended safely.

---

## How to Use The Architect

### Controls

- **T** - enter or exit text input mode.
- **Enter** - submit the current line to the Architect.
- **Esc** - cancel text input; from normal mode, closes the application.
- **Backspace** - delete the last character in the input line.

While the Architect processes a command, the renderer continues to run at
full frame rate. When the LLM completes, parsed commands are enqueued and
applied on the next frame.

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
- Token-to-text conversion uses the correct buffer sizing logic so that all
  generated tokens are preserved.
- The command queue is the only synchronization point between threads; ECS
  access is always performed on the main thread.

Phase 2 completes the Architect layer and provides a clean foundation for
future extensions such as richer command sets, multi-step tools, and tighter
integration with the diffusion pipeline.

