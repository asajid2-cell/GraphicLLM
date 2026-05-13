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

| Liquid Gallery | Water and Lava |
|---|---|
| ![Liquid Gallery](docs/media/liquid_gallery_hero.png) | ![Water and Lava](docs/media/liquid_gallery_water_lava.png) |

The capture manifest is [docs/media/gallery_manifest.json](docs/media/gallery_manifest.json).

Detail captures:

| Reflections | RT Materials | Metal Closeup | Glass and Emissive |
|---|---|---|---|
| ![RT Reflection Closeup](docs/media/rt_showcase_reflection_closeup.png) | ![RT Material Overview](docs/media/rt_showcase_material_overview.png) | ![Material Lab Metal Closeup](docs/media/material_lab_metal_closeup.png) | ![Material Lab Glass and Emissive](docs/media/material_lab_glass_emissive.png) |

| Material Prop Context | Pool Steps | Beach Props | Liquid Context |
|---|---|---|---|
| ![Material Lab Prop Context](docs/media/material_lab_prop_context.png) | ![Pool Steps and Coping](docs/media/glass_water_courtyard_pool_steps.png) | ![Beach Props and Shoreline](docs/media/outdoor_sunset_beach_life.png) | ![Liquid Gallery Context](docs/media/liquid_gallery_context.png) |

| Water | Glass Canopy | Particles | Neon Materials |
|---|---|---|---|
| ![Water Reflection Closeup](docs/media/glass_water_courtyard_water_closeup.png) | ![Glass Canopy Rim Light](docs/media/glass_water_courtyard_glass_canopy.png) | ![Particle and Bloom Closeup](docs/media/effects_showcase_particles_closeup.png) | ![Neon Materials](docs/media/effects_showcase_neon_materials.png) |

| Outdoor Waterline | Honey and Molasses | IBL Hero |
|---|---|---|
| ![Outdoor Waterline](docs/media/outdoor_sunset_beach_waterline.png) | ![Honey and Molasses](docs/media/liquid_gallery_viscous_pair.png) | ![IBL Gallery Hero](docs/media/ibl_gallery_hero.png) |

Short reel: [docs/media/cortex_gallery_reel.mp4](docs/media/cortex_gallery_reel.mp4)
with metadata in [docs/media/video_manifest.json](docs/media/video_manifest.json).

Regenerate it with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media
```

Regenerate the short gallery reel with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_gallery_reel.ps1
```

## What It Shows

- Hybrid DX12 rendering: visibility buffer, forward fallback, GPU culling,
  HZB, TAA, SSAO, SSR, bloom, tone mapping, and debug views.
- Ray tracing with contracts: scheduler intent, TLAS/material readiness,
  reflection dispatch readiness, raw reflection signal, and denoised history
  signal.
- Material coverage: mirror, glass, blue water, lava, honey, molasses, brushed
  metal, emissive, wet, anisotropic, clearcoat, transmission, sheen, and
  procedural-mask presets.
- Environment/IBL policy: manifest-driven environments, budget classes,
  runtime fallback behavior, and gallery validation.
- Public scenes: RT Showcase, Material Lab, Glass and Water Courtyard, Liquid
  Gallery, Effects Showcase, Outdoor Sunset Beach, and IBL Gallery.
- Release discipline: frame contracts, resource contracts, visual probes,
  package manifest checks, staged package launch smoke, and repo hygiene gates.

## Current Metrics

Source run:
`Z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build\bin\logs\runs\public_capture_gallery_20260513_003829_299_81552_3267d292`

| Scene | GPU ms | Capture | Render scale | Avg luma | Nonblack | RT signal/history |
|---|---:|---:|---:|---:|---:|---:|
| RT Showcase | 4.26 | 1920x1080 | 1.00 | 73.03 | 1.000 | 0.0105 / 0.0124 |
| RT Reflection Closeup | 5.22 | 1920x1080 | 1.00 | 72.70 | 1.000 | 0.0441 / 0.0479 |
| RT Material Overview | 4.09 | 1920x1080 | 1.00 | 108.57 | 1.000 | 0.0309 / 0.0310 |
| Material Lab | 4.00 | 1920x1080 | 1.00 | 179.72 | 1.000 | 0.0263 / 0.0273 |
| Material Lab Metal Closeup | 8.61 | 1920x1080 | 1.00 | 123.48 | 1.000 | 0.0457 / 0.0477 |
| Material Lab Glass and Emissive | 3.92 | 1920x1080 | 1.00 | 162.62 | 1.000 | 0.0814 / 0.0832 |
| Material Lab Prop Context | 3.99 | 1920x1080 | 1.00 | 160.97 | 1.000 | 0.0510 / 0.0527 |
| Glass and Water Courtyard | 15.20 | 1920x1080 | 1.00 | 182.80 | 1.000 | 0.0416 / 0.0419 |
| Water Reflection Closeup | 3.85 | 1920x1080 | 1.00 | 185.98 | 1.000 | 0.0020 / 0.0020 |
| Glass Canopy Rim Light | 3.89 | 1920x1080 | 1.00 | 144.11 | 1.000 | 0.0274 / 0.0275 |
| Pool Steps and Coping | 3.60 | 1920x1080 | 1.00 | 192.58 | 1.000 | 0.0526 / 0.0527 |
| Effects Showcase | 9.37 | 1920x1080 | 1.00 | 111.14 | 1.000 | 0.0055 / 0.0056 |
| Particle and Bloom Closeup | 4.72 | 1920x1080 | 1.00 | 95.16 | 1.000 | 0.0298 / 0.0301 |
| Neon Materials | 7.53 | 1920x1080 | 1.00 | 102.47 | 1.000 | 0.0142 / 0.0143 |
| Outdoor Sunset Beach | 4.04 | 1920x1080 | 1.00 | 205.63 | 1.000 | 0.0752 / 0.0774 |
| Outdoor Waterline | 4.37 | 1920x1080 | 1.00 | 205.35 | 1.000 | 0.1319 / 0.1351 |
| Beach Props and Shoreline | 8.85 | 1920x1080 | 1.00 | 183.07 | 1.000 | 0.1259 / 0.1285 |
| Liquid Gallery | 8.22 | 1920x1080 | 1.00 | 139.72 | 1.000 | 0.1540 / 0.1543 |
| Water and Lava | 7.08 | 1920x1080 | 1.00 | 112.23 | 1.000 | 0.2242 / 0.2246 |
| Honey and Molasses | 3.83 | 1920x1080 | 1.00 | 144.58 | 1.000 | 0.2158 / 0.2169 |
| Liquid Gallery Context | 4.21 | 1920x1080 | 1.00 | 141.42 | 1.000 | 0.2000 / 0.2007 |
| IBL Gallery Hero | 4.49 | 1920x1080 | 1.00 | 98.85 | 1.000 | 0.0419 / 0.0422 |
| IBL Gallery Sweep | 5.37 | 1920x1080 | 1.00 | 108.56 | 1.000 | 0.0309 / 0.0310 |

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
  but deeper content authoring and larger stress scenes remain ongoing renderer
  work rather than finished game content.

## License and Usage

Project Cortex is a learning and research project. Review the licenses of
third-party libraries and models before using it commercially.
