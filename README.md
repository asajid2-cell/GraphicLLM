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
`CortexEngine/build/bin/logs/runs/public_capture_gallery_20260512_133541_420_18928_97a03e95`

| Scene | GPU ms | Capture | Render scale | Avg luma | Nonblack | RT signal/history |
|---|---:|---:|---:|---:|---:|---:|
| RT Showcase | 4.38 | 1920x1080 | 1.00 | 70.31 | 1.000 | 0.0228 / 0.0295 |
| Material Lab | 4.17 | 1920x1080 | 1.00 | 183.15 | 1.000 | 0.0214 / 0.0224 |
| Glass and Water | 4.29 | 1920x1080 | 1.00 | 180.85 | 1.000 | 0.0392 / 0.0404 |
| Effects Showcase | 4.90 | 1920x1080 | 1.00 | 110.56 | 1.000 | 0.0042 / 0.0042 |
| Outdoor Sunset Beach | 3.41 | 1920x1080 | 1.00 | 169.96 | 1.000 | 0.0048 / 0.0048 |
| IBL Gallery | 4.60 | 1920x1080 | 1.00 | 107.89 | 1.000 | 0.0335 / 0.0336 |

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
- `CortexEngine/docs`: completion ledgers, media, and release evidence.
- `CortexEngine/tests`: focused C++ and script-driven contract tests.

## Release Notes

Current readiness notes are in
[`CortexEngine/RELEASE_READINESS.md`](CortexEngine/RELEASE_READINESS.md). The
public cleanup ledger is
[`CortexEngine/docs/RELEASE_CLEANUP_COMPLETION_LEDGER.md`](CortexEngine/docs/RELEASE_CLEANUP_COMPLETION_LEDGER.md).

## Limitations

The renderer requires Windows, DirectX 12, and a GPU/driver capable of the
enabled feature path. Optional AI/model assets are not required for the renderer
release package.
