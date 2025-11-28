# Agent Guidelines for This Repository

This repository is used as a portfolio-quality project.  
Agents working here should follow these guidelines:

---

## Scope of Work

- **Primary codebase:** `CortexEngine/`
  - This is the engine under active development.
- **External data / tools:**
  - `glTF-Sample-Models/`, `TensorRT/`, and `CortexEngine/models/` are
    external assets or third-party tools. Treat them as read-only unless the
    user explicitly asks otherwise.
  - Do not modify documentation under `TensorRT/` or `CortexEngine/vcpkg_installed/`
    unless the task is specifically about those projects.

---

## Documentation Style

When editing or creating documentation in `CortexEngine/` (e.g. `README.md`,
`QUICKSTART.md`, `PHASE*_*.md`, `*_ENHANCEMENTS.md`, `MATERIAL_SYSTEM.md`,
`SCRIPTS.md`):

- Use a **professional, concise** tone suitable for a résumé or portfolio.
- Avoid emojis, ASCII art boxes, “AI persona” language, or playful slang.
- Avoid non-standard encodings or decorative Unicode; prefer plain ASCII text.
- Emphasize:
  - High-level architecture (Core, Graphics, Scene, AI/LLM, Vision).
  - Render pipeline stages and major systems (IBL, SSAO, SSR, TAA/FXAA, bloom).
  - Phase roadmap and technical accomplishments.
- Keep sections logically structured (Overview → Architecture → Build → Usage).

If you detect garbled characters (e.g. `�`, `dYs?`, broken box drawing), clean
them up and replace them with clear, neutral wording.

---

## Git and Large Assets

- Respect the existing `.gitignore`:
  - **Do not** add large model files, HDR/EXR environment maps, or build
    outputs back into version control.
  - In particular, do not track:
    - `CortexEngine/models/`
    - `CortexEngine/assets/**/*.hdr`
    - `CortexEngine/assets/**/*.exr`
    - `CortexEngine/build/`, `CortexEngine/vcpkg_installed/`, or `CortexEngine/build/bin/`
- If new large assets are required for testing, place them under the existing
  ignored paths so they stay out of the repository.

---

## Code Changes

- Keep changes focused and minimal, matching the existing style.
- Preserve the separation of concerns:
  - Rendering logic in `src/Graphics`.
  - ECS and components in `src/Scene`.
  - LLM integration in `src/LLM`.
  - Diffusion logic in `src/AI/Vision`.
- When adding new features, prefer extending the existing patterns (e.g.
  command-based scene edits, post-processing passes) over introducing
  unrelated frameworks.

---

Following these guidelines helps keep the project clean, professional, and
easy to review as part of a résumé or portfolio.

