#!/usr/bin/env python3
"""Validate FullSceneLightingV3 shadow-source attribution captures.

The V3 lighting pass writes a dedicated attribution buffer:

- red: directional/sun shadow-loss ratio
- green: local fixture shadow-loss ratio
- blue: shadow-map path enabled

This analyzer checks that the source buffer is not merely present, but carries
signal that is consistent with the V3 shadow-loss and visibility debug views.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from analyze_full_scene_shader_debug_view_metrics import read_bmp_rgb


REQUIRED_VIEWS = {
    "source": "v3_shadow_source_attribution",
    "loss": "v3_shadow_loss",
    "visibility": "v3_shadow_visibility",
    "energy": "v3_lighting_energy_budget",
}


def _luma(pixel: tuple[int, int, int]) -> float:
    r, g, b = pixel
    return (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0


def _capture_path(result: dict[str, Any]) -> Path | None:
    capture = result.get("capture")
    if capture:
        return Path(str(capture))
    sequence = result.get("capture_sequence", [])
    if isinstance(sequence, list) and sequence:
        return Path(str(sequence[0]))
    return None


def _load_pixels(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    if not path.exists():
        raise FileNotFoundError(path)
    return read_bmp_rgb(path)


def _channel_stats(pixels: list[tuple[int, int, int]], threshold: float) -> dict[str, Any]:
    count = max(len(pixels), 1)
    threshold_255 = threshold * 255.0
    sums = [0.0, 0.0, 0.0]
    maxes = [0.0, 0.0, 0.0]
    active = [0, 0, 0]
    any_active = 0
    for pixel in pixels:
        any_channel = False
        for index, value in enumerate(pixel):
            normalized = value / 255.0
            sums[index] += normalized
            maxes[index] = max(maxes[index], normalized)
            if value > threshold_255:
                active[index] += 1
                any_channel = True
        if any_channel:
            any_active += 1
    return {
        "mean_rgb": [value / count for value in sums],
        "max_rgb": maxes,
        "active_rgb_ratio": [value / count for value in active],
        "active_any_ratio": any_active / count,
    }


def _luma_active_ratio(
    pixels: list[tuple[int, int, int]], threshold: float, *, invert: bool = False
) -> float:
    count = max(len(pixels), 1)
    active = 0
    for pixel in pixels:
        value = 1.0 - _luma(pixel) if invert else _luma(pixel)
        if value > threshold:
            active += 1
    return active / count


def _mean_luma(pixels: list[tuple[int, int, int]]) -> float:
    count = max(len(pixels), 1)
    return sum(_luma(pixel) for pixel in pixels) / count


def _views_by_family(manifest: dict[str, Any]) -> dict[str, dict[str, dict[str, Any]]]:
    views: dict[str, dict[str, dict[str, Any]]] = {}
    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        family = str(result.get("family", ""))
        view = str(result.get("view", ""))
        if family and view:
            views.setdefault(family, {})[view] = result
    return views


def build_report(
    manifest_path: Path,
    *,
    signal_threshold: float,
    min_enabled_ratio: float,
    min_source_active_ratio: float,
    min_loss_active_ratio: float,
    max_loss_without_source_ratio: float,
) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []

    for family, views in sorted(_views_by_family(manifest).items()):
        missing = [view for view in REQUIRED_VIEWS.values() if view not in views]
        if missing:
            failures.append(f"{family}: missing required shadow attribution views: {', '.join(missing)}")
            continue

        try:
            source_path = _capture_path(views[REQUIRED_VIEWS["source"]])
            loss_path = _capture_path(views[REQUIRED_VIEWS["loss"]])
            visibility_path = _capture_path(views[REQUIRED_VIEWS["visibility"]])
            energy_path = _capture_path(views[REQUIRED_VIEWS["energy"]])
            if not source_path or not loss_path or not visibility_path or not energy_path:
                raise ValueError("one or more required views has no capture path")

            source_width, source_height, source_pixels = _load_pixels(source_path)
            loss_width, loss_height, loss_pixels = _load_pixels(loss_path)
            visibility_width, visibility_height, visibility_pixels = _load_pixels(visibility_path)
            energy_width, energy_height, energy_pixels = _load_pixels(energy_path)
        except Exception as exc:  # noqa: BLE001 - include family and artifact context.
            failures.append(f"{family}: failed to load required captures: {exc}")
            continue

        dimensions = {
            (source_width, source_height),
            (loss_width, loss_height),
            (visibility_width, visibility_height),
            (energy_width, energy_height),
        }
        if len(dimensions) != 1:
            failures.append(f"{family}: required views have mismatched dimensions: {sorted(dimensions)}")
            continue

        source_stats = _channel_stats(source_pixels, signal_threshold)
        sun_shadow_loss_ratio = source_stats["mean_rgb"][0]
        local_shadow_loss_ratio = source_stats["mean_rgb"][1]
        shadow_map_enabled_ratio = source_stats["active_rgb_ratio"][2]
        source_active_ratio = sum(
            1
            for r, g, _b in source_pixels
            if max(r / 255.0, g / 255.0) > signal_threshold
        ) / max(len(source_pixels), 1)
        loss_active_ratio = _luma_active_ratio(loss_pixels, signal_threshold)
        visibility_occlusion_ratio = _luma_active_ratio(
            visibility_pixels,
            signal_threshold,
            invert=True,
        )
        energy_active_ratio = _luma_active_ratio(energy_pixels, signal_threshold)
        loss_mean_luma = _mean_luma(loss_pixels)
        visibility_mean_luma = _mean_luma(visibility_pixels)
        energy_mean_luma = _mean_luma(energy_pixels)

        status = "ok"
        family_failures: list[str] = []
        family_warnings: list[str] = []

        if shadow_map_enabled_ratio < min_enabled_ratio and loss_active_ratio >= min_loss_active_ratio:
            status = "shadow_loss_without_enabled_shadow_map"
            family_failures.append(
                f"shadow map enabled channel active ratio {shadow_map_enabled_ratio:.6f} "
                f"is below {min_enabled_ratio:.6f} while shadow loss is active"
            )
        if loss_active_ratio >= min_loss_active_ratio and source_active_ratio < min_source_active_ratio:
            status = "shadow_loss_without_source_attribution"
            family_failures.append(
                f"source active ratio {source_active_ratio:.6f} is below "
                f"{min_source_active_ratio:.6f} while shadow loss is active"
            )
        if source_active_ratio + max_loss_without_source_ratio < loss_active_ratio:
            status = "source_coverage_below_shadow_loss"
            family_warnings.append(
                f"source active ratio {source_active_ratio:.6f} trails shadow-loss active "
                f"ratio {loss_active_ratio:.6f} by more than {max_loss_without_source_ratio:.6f}"
            )
        if loss_active_ratio >= min_loss_active_ratio and visibility_occlusion_ratio <= 0.0:
            status = "shadow_loss_without_visibility_occlusion"
            family_warnings.append("shadow loss is active but visibility view has no darkened pixels")

        rows.append(
            {
                "family": family,
                "status": status,
                "source_capture": str(source_path),
                "loss_capture": str(loss_path),
                "visibility_capture": str(visibility_path),
                "energy_capture": str(energy_path),
                "width": source_width,
                "height": source_height,
                "sun_shadow_loss_ratio": sun_shadow_loss_ratio,
                "local_shadow_loss_ratio": local_shadow_loss_ratio,
                "shadow_map_enabled_ratio": shadow_map_enabled_ratio,
                "source_active_ratio": source_active_ratio,
                "shadow_loss_active_ratio": loss_active_ratio,
                "visibility_occlusion_ratio": visibility_occlusion_ratio,
                "energy_active_ratio": energy_active_ratio,
                "shadow_loss_mean_luma": loss_mean_luma,
                "shadow_visibility_mean_luma": visibility_mean_luma,
                "lighting_energy_mean_luma": energy_mean_luma,
                "source_stats": source_stats,
                "failures": family_failures,
                "warnings": family_warnings,
            }
        )
        failures.extend(f"{family}: {failure}" for failure in family_failures)
        warnings.extend(f"{family}: {warning}" for warning in family_warnings)

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.shadow_attribution.v1",
        "manifest": str(manifest_path),
        "signal_threshold": signal_threshold,
        "min_enabled_ratio": min_enabled_ratio,
        "min_source_active_ratio": min_source_active_ratio,
        "min_loss_active_ratio": min_loss_active_ratio,
        "max_loss_without_source_ratio": max_loss_without_source_ratio,
        "family_count": len(rows),
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Full Scene Shader V3 Shadow Attribution",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- families: {report['family_count']}",
        f"- failures: {len(report['failures'])}",
        f"- warnings: {len(report['warnings'])}",
        "",
        "| Family | Status | Sun Loss | Local Loss | Source Active | Loss Active | Visibility Occlusion | Shadow Map Enabled | Energy Active |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in report["rows"]:
        lines.append(
            "| {family} | {status} | {sun:.6f} | {local:.6f} | {source:.6f} | {loss:.6f} | {vis:.6f} | {enabled:.6f} | {energy:.6f} |".format(
                family=row["family"],
                status=row["status"],
                sun=row["sun_shadow_loss_ratio"],
                local=row["local_shadow_loss_ratio"],
                source=row["source_active_ratio"],
                loss=row["shadow_loss_active_ratio"],
                vis=row["visibility_occlusion_ratio"],
                enabled=row["shadow_map_enabled_ratio"],
                energy=row["energy_active_ratio"],
            )
        )
    if report["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in report["warnings"])
    if report["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-md", type=Path)
    parser.add_argument("--signal-threshold", type=float, default=0.02)
    parser.add_argument("--min-enabled-ratio", type=float, default=0.50)
    parser.add_argument("--min-source-active-ratio", type=float, default=0.001)
    parser.add_argument("--min-loss-active-ratio", type=float, default=0.001)
    parser.add_argument("--max-loss-without-source-ratio", type=float, default=0.50)
    parser.add_argument("--fail-on-warning", action="store_true")
    args = parser.parse_args()

    report = build_report(
        args.manifest,
        signal_threshold=args.signal_threshold,
        min_enabled_ratio=args.min_enabled_ratio,
        min_source_active_ratio=args.min_source_active_ratio,
        min_loss_active_ratio=args.min_loss_active_ratio,
        max_loss_without_source_ratio=args.max_loss_without_source_ratio,
    )
    output_json = args.output_json or args.manifest.with_name("v3_shadow_attribution.json")
    output_md = args.output_md or args.manifest.with_name("v3_shadow_attribution.md")
    output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, output_md)

    for warning in report["warnings"]:
        print(f"WARN: {warning}")
    for failure in report["failures"]:
        print(f"ERROR: {failure}", file=sys.stderr)

    if report["failures"]:
        return 1
    if args.fail_on_warning and report["warnings"]:
        return 1

    print(
        "PASS: V3 shadow attribution validated "
        f"{report['family_count']} families"
    )
    print(f"json={output_json}")
    print(f"markdown={output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
