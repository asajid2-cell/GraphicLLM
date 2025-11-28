# Material System

The Cortex Engine includes a physically based material system that can be driven
both programmatically and via LLM‑generated commands.

---

## Core Parameters

Each material is defined by the following parameters:

| Property     | Range   | Description                           |
|--------------|---------|---------------------------------------|
| `metallic`   | 0.0–1.0 | 0 = dielectric, 1 = conductor         |
| `roughness`  | 0.0–1.0 | 0 = mirror‑smooth, 1 = fully diffuse  |
| `ao`         | 0.0–1.0 | Ambient occlusion strength            |
| `albedo`     | RGBA    | Base surface color                    |

Typical presets:

```text
Shiny metal    = metallic 1.0, roughness 0.0–0.2
Brushed metal  = metallic 1.0, roughness 0.3–0.5
Matte plastic  = metallic 0.0, roughness 0.8–1.0
Smooth plastic = metallic 0.0, roughness 0.2–0.4
```

---

## Data Structures

### AddEntityCommand

Defined in `SceneCommands.h`:

```cpp
struct AddEntityCommand {
    glm::vec4 color{1.0f};
    float metallic  = 0.0f;
    float roughness = 0.5f;
    float ao        = 1.0f;
    // ...
};
```

When the command is executed, these values are propagated into
`RenderableComponent` and then into the shader constant buffer.

### MaterialConstants (GPU)

Defined in `ShaderTypes.h` and mirrored in HLSL:

```cpp
struct MaterialConstants {
    glm::vec4 albedo;
    float metallic;
    float roughness;
    float ao;
    // padding as needed for 16‑byte alignment
};
```

The renderer fills `MaterialConstants` per draw and binds it via a constant buffer.

---

## LLM Integration

The material system is exposed through the command layer so that prompts can
describe surface properties in natural language, for example:

- “Make it shiny metal.”
- “Make it matte plastic.”
- “Give it a brushed aluminum look.”

The LLM maps these phrases to concrete parameter values (metallic/roughness/AO)
inside `AddEntityCommand` or `ModifyMaterialCommand`. The parser:

- Normalizes ranges to `[0, 1]`.
- Applies sensible defaults when values are omitted.
- Clamps extreme values to keep lighting stable.

---

## Future Extensions

Planned enhancements include:

- Clearcoat and sheen terms for automotive and fabric materials.
- Emissive intensity and color.
- Support for normal/roughness/metallic texture maps.
- Additional presets for common real‑world materials.

