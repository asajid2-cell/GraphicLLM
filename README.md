# Project Cortex

Project Cortex is a real-time DirectX 12 hybrid renderer in
[`CortexEngine`](CortexEngine). It combines a visibility-buffer raster path with
ray-traced shadows, reflections, GI targets, temporal denoising, physically
classified materials, image-based lighting, particles, cinematic post controls,
and repeatable release validation.

The repository is organized around one primary artifact: the renderer. The
supporting scripts rebuild it, capture showcase scenes, validate frame contracts,
and create a public-review package.

## Screenshots

These captures were generated at 1920x1080 with the `public_high` graphics
preset.

| RT Showcase | Material Lab |
|---|---|
| ![RT Showcase](CortexEngine/docs/media/rt_showcase_hero.png) | ![Material Lab](CortexEngine/docs/media/material_lab_hero.png) |

| Glass and Water | Effects Showcase |
|---|---|
| ![Glass and Water Courtyard](CortexEngine/docs/media/glass_water_courtyard_hero.png) | ![Effects Showcase](CortexEngine/docs/media/effects_showcase_hero.png) |

| Outdoor Sunset Beach | IBL Gallery |
|---|---|
| ![Outdoor Sunset Beach](CortexEngine/docs/media/outdoor_sunset_beach_hero.png) | ![IBL Gallery](CortexEngine/docs/media/ibl_gallery_sweep.png) |

| Liquid Gallery | Water and Lava |
|---|---|
| ![Liquid Gallery](CortexEngine/docs/media/liquid_gallery_hero.png) | ![Water and Lava](CortexEngine/docs/media/liquid_gallery_water_lava.png) |

The capture manifest is
[`CortexEngine/docs/media/gallery_manifest.json`](CortexEngine/docs/media/gallery_manifest.json).

Detail captures:

| Reflections | RT Materials | Metal Closeup | Glass and Emissive |
|---|---|---|---|
| ![RT Reflection Closeup](CortexEngine/docs/media/rt_showcase_reflection_closeup.png) | ![RT Material Overview](CortexEngine/docs/media/rt_showcase_material_overview.png) | ![Material Lab Metal Closeup](CortexEngine/docs/media/material_lab_metal_closeup.png) | ![Material Lab Glass and Emissive](CortexEngine/docs/media/material_lab_glass_emissive.png) |

| Material Prop Context | Pool Steps | Beach Props | Liquid Context |
|---|---|---|---|
| ![Material Lab Prop Context](CortexEngine/docs/media/material_lab_prop_context.png) | ![Pool Steps and Coping](CortexEngine/docs/media/glass_water_courtyard_pool_steps.png) | ![Beach Props and Shoreline](CortexEngine/docs/media/outdoor_sunset_beach_life.png) | ![Liquid Gallery Context](CortexEngine/docs/media/liquid_gallery_context.png) |

| Water | Glass Canopy | Particles | Neon Materials |
|---|---|---|---|
| ![Water Reflection Closeup](CortexEngine/docs/media/glass_water_courtyard_water_closeup.png) | ![Glass Canopy Rim Light](CortexEngine/docs/media/glass_water_courtyard_glass_canopy.png) | ![Particle and Bloom Closeup](CortexEngine/docs/media/effects_showcase_particles_closeup.png) | ![Neon Materials](CortexEngine/docs/media/effects_showcase_neon_materials.png) |

| Outdoor Waterline | Honey and Molasses | IBL Hero |
|---|---|---|
| ![Outdoor Waterline](CortexEngine/docs/media/outdoor_sunset_beach_waterline.png) | ![Honey and Molasses](CortexEngine/docs/media/liquid_gallery_viscous_pair.png) | ![IBL Gallery Hero](CortexEngine/docs/media/ibl_gallery_hero.png) |

Short reel:
[`CortexEngine/docs/media/cortex_gallery_reel.mp4`](CortexEngine/docs/media/cortex_gallery_reel.mp4)
with metadata in
[`CortexEngine/docs/media/video_manifest.json`](CortexEngine/docs/media/video_manifest.json).

## What It Shows

- Hybrid DX12 rendering: visibility buffer, forward fallback, GPU culling, HZB,
  TAA, SSAO, SSR, bloom, tone mapping, and debug views.
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

## Quick Start

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/setup.ps1
powershell -ExecutionPolicy Bypass -File CortexEngine/run.ps1
```

High-quality showcase run:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/run.ps1 -Preset public_high -Scene rt_showcase
```

## Run Modes

- `safe_startup`: conservative default for basic launch validation.
- `release_showcase`: balanced public showcase preset used by release smokes.
- `public_high`: high-resolution capture preset with full render scale.

Regenerate the screenshot gallery:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_capture_gallery.ps1 -NoBuild -Quality High -OutputDir CortexEngine/docs/media -Width 1920 -Height 1080
```

Regenerate the short gallery reel:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_public_gallery_reel.ps1
```

## Validation

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

Release validation covers rebuild, renderer ownership checks, RT and temporal
smokes, budget/profile matrices, material and graphics UI contracts, public
scene probes, package checks, and staged package launch smoke.

## Architecture

- `CortexEngine/src`: engine, renderer, scene, UI, and runtime code.
- `CortexEngine/assets`: shaders, configuration, environment manifests, and
  runtime textures.
- `CortexEngine/tools`: validation, capture, packaging, and release scripts.
- `CortexEngine/docs/media`: committed screenshots, video, and media manifests.
- `CortexEngine/tests`: focused C++ and script-driven contract tests.

## Release Notes

Current readiness notes are in
[`CortexEngine/RELEASE_READINESS.md`](CortexEngine/RELEASE_READINESS.md).

## Limitations

The renderer requires Windows, DirectX 12, and a GPU/driver capable of the
enabled feature path. Optional AI/model assets are not required for the renderer
release package.
