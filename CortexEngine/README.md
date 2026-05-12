# Project Cortex

Project Cortex is a real-time DirectX 12 hybrid renderer. It combines a
visibility-buffer raster path with ray-traced shadows, reflections, GI targets,
temporal denoising, physically classified materials, image-based lighting,
particles, cinematic post controls, and repeatable release validation.

The project also includes optional LLM and diffusion-texture tooling, but the
main artifact is the renderer: measurable frames, public showcase scenes,
runtime contracts, and package checks that can be rebuilt and rerun locally.

## Screenshots

These captures were generated at 1920x1080 with the `public_high` graphics
preset:

| RT Showcase | Material Lab |
|---|---|
| ![RT Showcase](docs/media/rt_showcase_hero.png) | ![Material Lab](docs/media/material_lab_hero.png) |

| Glass and Water | Effects Showcase |
|---|---|
| ![Glass and Water Courtyard](docs/media/glass_water_courtyard_hero.png) | ![Effects Showcase](docs/media/effects_showcase_hero.png) |

| Outdoor Sunset Beach | IBL Gallery |
|---|---|
| ![Outdoor Sunset Beach](docs/media/outdoor_sunset_beach_hero.png) | ![IBL Gallery](docs/media/ibl_gallery_sweep.png) |

The capture manifest is [docs/media/gallery_manifest.json](docs/media/gallery_manifest.json).
Regenerate it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media
```

## What It Shows

- Hybrid DX12 rendering: visibility buffer, forward fallback, GPU culling,
  HZB, TAA, SSAO, SSR, bloom, tone mapping, and debug views.
- Ray tracing with contracts: scheduler intent, TLAS/material readiness,
  reflection dispatch readiness, raw reflection signal, and denoised history
  signal.
- Material coverage: mirror, glass, water, brushed metal, emissive, wet,
  anisotropic, clearcoat, transmission, sheen, and procedural-mask presets.
- Environment/IBL policy: manifest-driven environments, budget classes,
  runtime fallback behavior, and gallery validation.
- Public scenes: RT Showcase, Material Lab, Glass and Water Courtyard,
  Effects Showcase, Outdoor Sunset Beach, and IBL Gallery.
- Release discipline: frame contracts, resource contracts, visual probes,
  package manifest checks, staged package launch smoke, and repo hygiene gates.

## Current Metrics

Source run:
`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260512_132733_943_27296_0d962fbc`

| Scene | GPU ms | Capture | Render scale | Avg luma | Nonblack | RT signal/history |
|---|---:|---:|---:|---:|---:|---:|
| RT Showcase | 4.53 | 1920x1080 | 1.00 | 70.34 | 1.000 | 0.0228 / 0.0295 |
| Material Lab | 4.17 | 1920x1080 | 1.00 | 183.15 | 1.000 | 0.0214 / 0.0224 |
| Glass and Water | 5.06 | 1920x1080 | 1.00 | 180.85 | 1.000 | 0.0392 / 0.0404 |
| Effects Showcase | 4.75 | 1920x1080 | 1.00 | 110.52 | 1.000 | 0.0042 / 0.0042 |
| Outdoor Sunset Beach | 3.38 | 1920x1080 | 1.00 | 169.95 | 1.000 | 0.0048 / 0.0048 |
| IBL Gallery | 4.65 | 1920x1080 | 1.00 | 107.89 | 1.000 | 0.0335 / 0.0336 |

Release validation also checks descriptor pressure, memory budgets, RT budget
profiles, visual probes, and package launch behavior. See
[RELEASE_READINESS.md](RELEASE_READINESS.md) for the current gate summary.

## Quick Start

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/setup.ps1
powershell -ExecutionPolicy Bypass -File CortexEngine/run.ps1
```

From `CortexEngine`, the usual local run is:

```powershell
.\run.ps1
```

## Run Modes

Default startup is meant to be robust on a wider range of machines. Public
captures and validation use explicit presets so the output is reproducible.

```powershell
# Safe startup profile used by package launch smoke
.\build\bin\CortexEngine.exe --scene temporal_validation --graphics-preset safe_startup --environment studio --no-llm --no-dreamer --no-launcher

# Release showcase profile used by runtime gates
.\build\bin\CortexEngine.exe --scene rt_showcase --camera-bookmark hero --graphics-preset release_showcase --environment studio --no-llm --no-dreamer --no-launcher

# High-resolution public capture profile
.\build\bin\CortexEngine.exe --scene rt_showcase --camera-bookmark hero --graphics-preset public_high --environment studio --window-width 1920 --window-height 1080 --no-llm --no-dreamer --no-launcher
```

## Validation

Run the full local release gate from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The gate rebuilds Release and runs renderer contracts, temporal/RT smokes,
visual probes, graphics UI tests, material and IBL checks, effects checks,
ownership audits, package manifest validation, staged package launch smoke,
budget profiles, and repo hygiene.

Useful focused gates:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_visual_probe_validation.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_phase3_visual_matrix.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_package_contract_tests.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_package_launch_smoke.ps1 -NoBuild
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_readme_contract_tests.ps1
```

## Architecture

- `src/Core`: engine loop, startup preflight, windowing, camera automation,
  frame reports, and release diagnostics.
- `src/Graphics`: DX12 renderer, RHI, render graph passes, RT scheduling,
  material resolution, environment/IBL, particles, post processing, and
  frame/resource contracts.
- `src/Scene`: EnTT-based scene registry and components.
- `src/UI`: native graphics controls, material controls, performance views,
  and editor-facing tools.
- `src/AI`: optional LLM scene commands and optional diffusion texture
  generation paths.
- `tools`: repeatable build, validation, screenshot, package, and release
  readiness scripts.

## Packaging

The public-review payload is manifest-driven by
`assets/config/release_package_manifest.json`.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_create_release_package.ps1 -NoBuild
```

The package scripts exclude local models, logs, build caches, debug artifacts,
and source HDR/EXR environment files. The staged package launch smoke copies the
selected runtime payload to an isolated directory and launches it from there.

## Limitations

- Validation numbers are hardware dependent. The current local evidence was
  produced on a Windows/DX12 machine with an NVIDIA RTX 3070 Ti.
- The LLM and Dreamer texture paths are optional. Most renderer gates run with
  `--no-llm --no-dreamer`.
- The voxel backend is experimental and smoke-tested; it is not the main
  renderer path.
- Materials, effects, and cinematic post are validated as public foundations,
  but further parity matrices and stress scenes are tracked in
  `docs/MATERIALS_GRAPHICS_ROBUSTNESS_LEDGER.md`.

## License and Usage

Project Cortex is a learning and research project. Review the licenses of
third-party libraries and models before using it commercially.
