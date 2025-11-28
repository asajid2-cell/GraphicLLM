# Cortex Engine - Feature Enhancements Summary

This document summarizes key enhancements made to the LLM-driven graphics
workflow in Project Cortex.

---

## Extended Color Vocabulary

The engine supports a broad set of color names so that prompts can remain natural.
Colors are grouped conceptually:

- **Primary** - red, blue, green, yellow, cyan, magenta
- **Secondary** - orange, purple, pink, lime, teal, violet
- **Tertiary** - brown, tan, beige, maroon, olive, navy, aqua, turquoise, gold, silver, bronze
- **Grayscale** - white, black, gray/grey, light gray, dark gray

Example prompts:

- "Make it gold."
- "Turn the sphere turquoise."
- "Change the cube to navy blue."

The parser maps these names to normalized RGBA values before updating materials.

---

## Additional Primitive Shapes

Beyond the original cube and sphere, the command system can describe several
new primitives (depending on configuration), such as:

- Plane
- Cylinder
- Cone
- Torus

Each primitive is mapped to either a built-in mesh generator or a pre-loaded
asset in the scene.

---

## Transform and Layout Commands

The command set has been extended to understand simple spatial relationships:

- "Next to", "above", "below", "in front of", etc.
- Relative scaling ("twice as big", "half as tall").
- Simple alignment ("center it", "place it on the floor").

These phrases are translated into concrete position and scale values in the
relevant `ModifyTransformCommand`.

---

## Robustness Improvements

Several improvements were made to handle imperfect model output:

- Tolerant parsing of color and shape names (case-insensitive, minor typos).
- Clamping of numeric values (e.g., metallic/roughness/AO in `[0, 1]`).
- Safer handling of missing optional fields in JSON.

These enhancements make it harder for a slightly "noisy" response to result
in invalid scene state.

