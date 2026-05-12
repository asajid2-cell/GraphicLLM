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

The capture manifest is
[`CortexEngine/docs/media/gallery_manifest.json`](CortexEngine/docs/media/gallery_manifest.json).

Detail captures:

| Reflections | RT Materials | Metal Closeup | Glass and Emissive |
|---|---|---|---|
| ![RT Reflection Closeup](CortexEngine/docs/media/rt_showcase_reflection_closeup.png) | ![RT Material Overview](CortexEngine/docs/media/rt_showcase_material_overview.png) | ![Material Lab Metal Closeup](CortexEngine/docs/media/material_lab_metal_closeup.png) | ![Material Lab Glass and Emissive](CortexEngine/docs/media/material_lab_glass_emissive.png) |

| Water | Glass Canopy | Particles | Neon Materials |
|---|---|---|---|
| ![Water Reflection Closeup](CortexEngine/docs/media/glass_water_courtyard_water_closeup.png) | ![Glass Canopy Rim Light](CortexEngine/docs/media/glass_water_courtyard_glass_canopy.png) | ![Particle and Bloom Closeup](CortexEngine/docs/media/effects_showcase_particles_closeup.png) | ![Neon Materials](CortexEngine/docs/media/effects_showcase_neon_materials.png) |

| Outdoor Waterline | IBL Hero |
|---|---|
| ![Outdoor Waterline](CortexEngine/docs/media/outdoor_sunset_beach_waterline.png) | ![IBL Gallery Hero](CortexEngine/docs/media/ibl_gallery_hero.png) |

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
- Material coverage: mirror, glass, water, brushed metal, emissive, wet,
  anisotropic, clearcoat, transmission, sheen, and procedural-mask presets.
- Environment/IBL policy: manifest-driven environments, budget classes,
  runtime fallback behavior, and gallery validation.
- Public scenes: RT Showcase, Material Lab, Glass and Water Courtyard, Effects
  Showcase, Outdoor Sunset Beach, and IBL Gallery.

## Current Metrics

Source run:
`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260512_151920_699_36360_0cb5c88e`

| Scene | GPU ms | Capture | Render scale | Avg luma | Nonblack | RT signal/history |
|---|---:|---:|---:|---:|---:|---:|
| RT Showcase | 4.02 | 1920x1080 | 1.00 | 70.34 | 1.000 | 0.0228 / 0.0295 |
| RT Reflection Closeup | 8.17 | 1920x1080 | 1.00 | 55.78 | 1.000 | 0.0293 / 0.0294 |
| RT Material Overview | 4.61 | 1920x1080 | 1.00 | 107.89 | 1.000 | 0.0335 / 0.0336 |
| Material Lab | 4.77 | 1920x1080 | 1.00 | 183.16 | 1.000 | 0.0214 / 0.0224 |
| Material Lab Metal Closeup | 3.59 | 1920x1080 | 1.00 | 134.23 | 1.000 | 0.0752 / 0.0765 |
| Material Lab Glass and Emissive | 3.63 | 1920x1080 | 1.00 | 169.42 | 1.000 | 0.0942 / 0.0956 |
| Glass and Water Courtyard | 3.78 | 1920x1080 | 1.00 | 180.86 | 1.000 | 0.0392 / 0.0404 |
| Water Reflection Closeup | 3.69 | 1920x1080 | 1.00 | 197.19 | 1.000 | 0.0549 / 0.0580 |
| Glass Canopy Rim Light | 3.60 | 1920x1080 | 1.00 | 140.52 | 1.000 | 0.0171 / 0.0181 |
| Effects Showcase | 3.99 | 1920x1080 | 1.00 | 110.48 | 1.000 | 0.0042 / 0.0042 |
| Particle and Bloom Closeup | 4.27 | 1920x1080 | 1.00 | 92.98 | 1.000 | 0.0288 / 0.0290 |
| Neon Materials | 4.05 | 1920x1080 | 1.00 | 94.46 | 1.000 | 0.0110 / 0.0111 |
| Outdoor Sunset Beach | 3.03 | 1920x1080 | 1.00 | 169.96 | 1.000 | 0.0048 / 0.0048 |
| Outdoor Waterline | 2.94 | 1920x1080 | 1.00 | 181.49 | 1.000 | 0.0162 / 0.0162 |
| IBL Gallery Hero | 4.05 | 1920x1080 | 1.00 | 101.72 | 1.000 | 0.0420 / 0.0423 |
| IBL Gallery Sweep | 4.07 | 1920x1080 | 1.00 | 107.90 | 1.000 | 0.0335 / 0.0336 |

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
