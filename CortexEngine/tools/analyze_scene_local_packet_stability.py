#!/usr/bin/env python3
"""Analyze adjacent-frame stability for scene-local cinematic packets.

The packet runner can capture a short sequence for each debug view. This tool
compares consecutive captures and writes per-family/view stability metrics into
the manifest so renderer flicker is visible in the same evidence packet as
material and reflection ownership.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from PIL import Image


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path: Path, data: Any) -> None:
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def resolve_path(path: str, base: Path) -> Path:
    p = Path(path)
    if p.is_absolute():
        return p
    return (base / p).resolve()


def capture_sequence(row: dict[str, Any], manifest_base: Path) -> list[Path]:
    explicit = row.get("capture_sequence")
    if isinstance(explicit, list) and explicit:
        return [
            resolve_path(str(item), manifest_base)
            for item in explicit
            if str(item or "").strip()
        ]

    log_dir = str(row.get("log_dir") or "").strip()
    if log_dir:
        p = resolve_path(log_dir, manifest_base)
        if p.exists():
            return sorted(p.glob("visual_validation_frame_*.bmp"))

    capture = str(row.get("capture") or "").strip()
    if capture:
        return [resolve_path(capture, manifest_base)]
    return []


def load_luma(path: Path, *, max_dimension: int) -> tuple[int, int, list[float]]:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        original_width, original_height = rgb.size
        if max(original_width, original_height) > max_dimension:
            scale = max_dimension / float(max(original_width, original_height))
            rgb = rgb.resize(
                (
                    max(1, int(round(original_width * scale))),
                    max(1, int(round(original_height * scale))),
                ),
                Image.Resampling.BILINEAR,
            )
        width, height = rgb.size
        luma: list[float] = []
        pixels = rgb.load()
        for y in range(height):
            for x in range(width):
                r, g, b = pixels[x, y]
                luma.append((0.2126 * r) + (0.7152 * g) + (0.0722 * b))
    return width, height, luma


def compare_luma_with_offset(
    luma_a: list[float],
    luma_b: list[float],
    width: int,
    height: int,
    *,
    dx: int,
    dy: int,
    changed_threshold: float,
    large_changed_threshold: float,
) -> dict[str, Any]:
    x0_a = max(0, -dx)
    y0_a = max(0, -dy)
    x0_b = max(0, dx)
    y0_b = max(0, dy)
    overlap_width = width - abs(dx)
    overlap_height = height - abs(dy)
    if overlap_width <= 0 or overlap_height <= 0:
        return {
            "valid": False,
            "reason": "empty_overlap",
            "dx": dx,
            "dy": dy,
            "overlap_ratio": 0.0,
        }

    pixel_count = overlap_width * overlap_height
    sum_abs = 0.0
    changed = 0
    large_changed = 0
    for row in range(overlap_height):
        a_base = (y0_a + row) * width + x0_a
        b_base = (y0_b + row) * width + x0_b
        for col in range(overlap_width):
            delta = abs(luma_a[a_base + col] - luma_b[b_base + col])
            sum_abs += delta
            if delta > changed_threshold:
                changed += 1
            if delta > large_changed_threshold:
                large_changed += 1

    full_pixels = max(1, width * height)
    return {
        "valid": True,
        "dx": dx,
        "dy": dy,
        "overlap_width": overlap_width,
        "overlap_height": overlap_height,
        "pixel_count": pixel_count,
        "overlap_ratio": pixel_count / float(full_pixels),
        "mean_abs_luma_delta": sum_abs / float(max(1, pixel_count)),
        "changed_pixel_ratio": changed / float(max(1, pixel_count)),
        "large_changed_pixel_ratio": large_changed / float(max(1, pixel_count)),
    }


def luma_edge_mask(
    luma: list[float],
    width: int,
    height: int,
    *,
    threshold: float,
    dilation: int,
) -> list[bool]:
    mask = [False] * max(0, width * height)
    if width <= 2 or height <= 2:
        return mask

    for y in range(1, height - 1):
        row = y * width
        for x in range(1, width - 1):
            idx = row + x
            center = luma[idx]
            grad = max(
                abs(center - luma[idx - 1]),
                abs(center - luma[idx + 1]),
                abs(center - luma[idx - width]),
                abs(center - luma[idx + width]),
            )
            if grad > threshold:
                mask[idx] = True

    if dilation <= 0:
        return mask

    dilated = mask[:]
    for y in range(height):
        for x in range(width):
            if not mask[y * width + x]:
                continue
            y0 = max(0, y - dilation)
            y1 = min(height - 1, y + dilation)
            x0 = max(0, x - dilation)
            x1 = min(width - 1, x + dilation)
            for yy in range(y0, y1 + 1):
                base = yy * width
                for xx in range(x0, x1 + 1):
                    dilated[base + xx] = True
    return dilated


def compare_luma_stable_core_with_offset(
    luma_a: list[float],
    luma_b: list[float],
    width: int,
    height: int,
    *,
    dx: int,
    dy: int,
    changed_threshold: float,
    large_changed_threshold: float,
    edge_threshold: float,
    edge_dilation: int,
) -> dict[str, Any]:
    x0_a = max(0, -dx)
    y0_a = max(0, -dy)
    x0_b = max(0, dx)
    y0_b = max(0, dy)
    overlap_width = width - abs(dx)
    overlap_height = height - abs(dy)
    if overlap_width <= 0 or overlap_height <= 0:
        return {
            "valid": False,
            "reason": "empty_overlap",
            "dx": dx,
            "dy": dy,
            "overlap_ratio": 0.0,
        }

    edges_a = luma_edge_mask(
        luma_a,
        width,
        height,
        threshold=edge_threshold,
        dilation=edge_dilation,
    )
    edges_b = luma_edge_mask(
        luma_b,
        width,
        height,
        threshold=edge_threshold,
        dilation=edge_dilation,
    )

    full_pixels = max(1, width * height)
    overlap_pixels = overlap_width * overlap_height
    core_pixels = 0
    edge_pixels = 0
    sum_abs = 0.0
    changed = 0
    large_changed = 0
    for row in range(overlap_height):
        a_base = (y0_a + row) * width + x0_a
        b_base = (y0_b + row) * width + x0_b
        for col in range(overlap_width):
            idx_a = a_base + col
            idx_b = b_base + col
            if edges_a[idx_a] or edges_b[idx_b]:
                edge_pixels += 1
                continue
            delta = abs(luma_a[idx_a] - luma_b[idx_b])
            sum_abs += delta
            core_pixels += 1
            if delta > changed_threshold:
                changed += 1
            if delta > large_changed_threshold:
                large_changed += 1

    if core_pixels <= 0:
        return {
            "valid": False,
            "reason": "empty_stable_core",
            "dx": dx,
            "dy": dy,
            "overlap_ratio": overlap_pixels / float(full_pixels),
            "stable_core_ratio": 0.0,
            "edge_rejected_ratio": edge_pixels / float(max(1, overlap_pixels)),
        }

    return {
        "valid": True,
        "dx": dx,
        "dy": dy,
        "overlap_width": overlap_width,
        "overlap_height": overlap_height,
        "pixel_count": core_pixels,
        "overlap_ratio": overlap_pixels / float(full_pixels),
        "stable_core_ratio": core_pixels / float(full_pixels),
        "edge_rejected_ratio": edge_pixels / float(max(1, overlap_pixels)),
        "mean_abs_luma_delta": sum_abs / float(core_pixels),
        "changed_pixel_ratio": changed / float(core_pixels),
        "large_changed_pixel_ratio": large_changed / float(core_pixels),
    }


def find_best_global_luma_offset(
    luma_a: list[float],
    luma_b: list[float],
    width: int,
    height: int,
    *,
    max_shift: int,
    changed_threshold: float,
    large_changed_threshold: float,
    min_overlap_ratio: float = 0.70,
) -> dict[str, Any]:
    best: dict[str, Any] | None = None
    for dy in range(-max_shift, max_shift + 1):
        for dx in range(-max_shift, max_shift + 1):
            comparison = compare_luma_with_offset(
                luma_a,
                luma_b,
                width,
                height,
                dx=dx,
                dy=dy,
                changed_threshold=changed_threshold,
                large_changed_threshold=large_changed_threshold,
            )
            if not comparison.get("valid"):
                continue
            if float(comparison["overlap_ratio"]) < min_overlap_ratio:
                continue
            if best is None:
                best = comparison
                continue
            score = float(comparison["mean_abs_luma_delta"])
            best_score = float(best["mean_abs_luma_delta"])
            if score < best_score - 1e-6:
                best = comparison
            elif abs(score - best_score) <= 1e-6 and float(comparison["overlap_ratio"]) > float(best["overlap_ratio"]):
                best = comparison
    if best is None:
        return {
            "valid": False,
            "reason": "no_alignment_candidate",
            "max_shift": max_shift,
        }
    return best


def compare_images(
    a: Path,
    b: Path,
    *,
    max_dimension: int,
    changed_threshold: float,
    large_changed_threshold: float,
    motion_compensate: bool,
    alignment_max_dimension: int,
    max_alignment_shift: int,
    edge_threshold: float,
    edge_dilation: int,
) -> dict[str, Any]:
    width_a, height_a, luma_a = load_luma(a, max_dimension=max_dimension)
    width_b, height_b, luma_b = load_luma(b, max_dimension=max_dimension)
    if width_a != width_b or height_a != height_b:
        return {
            "valid": False,
            "reason": "dimension_mismatch",
            "from": str(a),
            "to": str(b),
            "width_a": width_a,
            "height_a": height_a,
            "width_b": width_b,
            "height_b": height_b,
        }

    pixel_count = max(1, len(luma_a))
    sum_abs = 0.0
    changed = 0
    large_changed = 0
    for va, vb in zip(luma_a, luma_b):
        delta = abs(va - vb)
        sum_abs += delta
        if delta > changed_threshold:
            changed += 1
        if delta > large_changed_threshold:
            large_changed += 1

    result = {
        "valid": True,
        "from": str(a),
        "to": str(b),
        "width": width_a,
        "height": height_a,
        "pixel_count": pixel_count,
        "mean_abs_luma_delta": sum_abs / float(pixel_count),
        "changed_pixel_ratio": changed / float(pixel_count),
        "large_changed_pixel_ratio": large_changed / float(pixel_count),
    }
    if motion_compensate:
        align_width_a, align_height_a, align_luma_a = load_luma(
            a,
            max_dimension=alignment_max_dimension,
        )
        align_width_b, align_height_b, align_luma_b = load_luma(
            b,
            max_dimension=alignment_max_dimension,
        )
        if align_width_a == align_width_b and align_height_a == align_height_b:
            aligned = find_best_global_luma_offset(
                align_luma_a,
                align_luma_b,
                align_width_a,
                align_height_a,
                max_shift=max_alignment_shift,
                changed_threshold=changed_threshold,
                large_changed_threshold=large_changed_threshold,
            )
            result["motion_compensated"] = {
                "valid": bool(aligned.get("valid")),
                "alignment_width": align_width_a,
                "alignment_height": align_height_a,
                "max_alignment_shift": max_alignment_shift,
                **aligned,
            }
            if aligned.get("valid"):
                scale_x = width_a / float(max(1, align_width_a))
                scale_y = height_a / float(max(1, align_height_a))
                full_dx = int(round(float(aligned.get("dx", 0)) * scale_x))
                full_dy = int(round(float(aligned.get("dy", 0)) * scale_y))
                # Low-resolution alignment is intentionally coarse; search a
                # small full-resolution neighborhood and keep the lowest mean
                # residual for stable-core analysis.
                best_core: dict[str, Any] | None = None
                for ddy in range(full_dy - 1, full_dy + 2):
                    for ddx in range(full_dx - 1, full_dx + 2):
                        core = compare_luma_stable_core_with_offset(
                            luma_a,
                            luma_b,
                            width_a,
                            height_a,
                            dx=ddx,
                            dy=ddy,
                            changed_threshold=changed_threshold,
                            large_changed_threshold=large_changed_threshold,
                            edge_threshold=edge_threshold,
                            edge_dilation=edge_dilation,
                        )
                        if not core.get("valid"):
                            continue
                        if best_core is None:
                            best_core = core
                            continue
                        if float(core["mean_abs_luma_delta"]) < float(best_core["mean_abs_luma_delta"]) - 1e-6:
                            best_core = core
                result["motion_stable_core"] = best_core or {
                    "valid": False,
                    "reason": "no_stable_core_alignment",
                    "scaled_dx": full_dx,
                    "scaled_dy": full_dy,
                }
        else:
            result["motion_compensated"] = {
                "valid": False,
                "reason": "alignment_dimension_mismatch",
                "width_a": align_width_a,
                "height_a": align_height_a,
                "width_b": align_width_b,
                "height_b": align_height_b,
            }

    return result


def analyze_manifest(
    manifest_path: Path,
    *,
    max_mean_abs_luma_delta: float,
    max_changed_pixel_ratio: float,
    max_large_changed_pixel_ratio: float,
    warn_mean_abs_luma_delta: float,
    warn_changed_pixel_ratio: float,
    warn_large_changed_pixel_ratio: float,
    warn_motion_compensated_mean_abs_luma_delta: float,
    warn_motion_compensated_changed_pixel_ratio: float,
    warn_motion_compensated_large_changed_pixel_ratio: float,
    changed_threshold: float,
    large_changed_threshold: float,
    max_dimension: int,
    alignment_max_dimension: int,
    max_alignment_shift: int,
    edge_threshold: float,
    edge_dilation: int,
    min_stable_core_ratio: float,
) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    manifest_base = manifest_path.parent
    rows = list(manifest.get("results", []))
    motion_mode = str(manifest.get("stability_motion_mode") or "static")
    motion_packet = motion_mode != "static"
    motion_warning_only_views = {"taa_blend", "reflection_probe_weight"}
    motion_informational_views = {"reflection_probe_weight"}

    failures: list[str] = []
    warnings: list[str] = []
    diagnostic_signals: list[str] = []
    reports: list[dict[str, Any]] = []

    for row in rows:
        family = str(row.get("family") or "unknown")
        view = str(row.get("view") or "unknown")
        hard_gate_view = not (
            motion_packet and view in motion_warning_only_views
        )
        informational_view = motion_packet and view in motion_informational_views
        sequence = capture_sequence(row, manifest_base)
        row_failures: list[str] = []
        row_warnings: list[str] = []
        row_diagnostic_signals: list[str] = []
        comparisons: list[dict[str, Any]] = []

        if len(sequence) < 2:
            row_warnings.append("capture_sequence_too_short")
        else:
            for idx in range(len(sequence) - 1):
                if not sequence[idx].exists() or not sequence[idx + 1].exists():
                    row_failures.append(
                        f"capture_missing:{sequence[idx]}->{sequence[idx + 1]}"
                    )
                    continue
                comparison = compare_images(
                    sequence[idx],
                    sequence[idx + 1],
                    max_dimension=max_dimension,
                    changed_threshold=changed_threshold,
                    large_changed_threshold=large_changed_threshold,
                    motion_compensate=motion_packet,
                    alignment_max_dimension=alignment_max_dimension,
                    max_alignment_shift=max_alignment_shift,
                    edge_threshold=edge_threshold,
                    edge_dilation=edge_dilation,
                )
                comparisons.append(comparison)
                if not comparison.get("valid"):
                    row_failures.append(
                        f"{sequence[idx].name}->{sequence[idx + 1].name}:{comparison.get('reason')}"
                    )

        valid_comparisons = [item for item in comparisons if item.get("valid")]
        max_mean = max(
            [float(item["mean_abs_luma_delta"]) for item in valid_comparisons],
            default=0.0,
        )
        max_changed = max(
            [float(item["changed_pixel_ratio"]) for item in valid_comparisons],
            default=0.0,
        )
        max_large = max(
            [float(item["large_changed_pixel_ratio"]) for item in valid_comparisons],
            default=0.0,
        )
        compensated = [
            item.get("motion_compensated", {})
            for item in valid_comparisons
            if item.get("motion_compensated", {}).get("valid")
        ]
        stable_core = [
            item.get("motion_stable_core", {})
            for item in valid_comparisons
            if item.get("motion_stable_core", {}).get("valid") and
            float(item.get("motion_stable_core", {}).get("stable_core_ratio", 0.0)) >= min_stable_core_ratio
        ]
        max_compensated_mean = max(
            [float(item["mean_abs_luma_delta"]) for item in compensated],
            default=max_mean,
        )
        max_compensated_changed = max(
            [float(item["changed_pixel_ratio"]) for item in compensated],
            default=max_changed,
        )
        max_compensated_large = max(
            [float(item["large_changed_pixel_ratio"]) for item in compensated],
            default=max_large,
        )
        compensated_overlap_min = min(
            [float(item["overlap_ratio"]) for item in compensated],
            default=0.0,
        )
        max_stable_core_mean = max(
            [float(item["mean_abs_luma_delta"]) for item in stable_core],
            default=max_compensated_mean,
        )
        max_stable_core_changed = max(
            [float(item["changed_pixel_ratio"]) for item in stable_core],
            default=max_compensated_changed,
        )
        max_stable_core_large = max(
            [float(item["large_changed_pixel_ratio"]) for item in stable_core],
            default=max_compensated_large,
        )
        stable_core_ratio_min = min(
            [float(item["stable_core_ratio"]) for item in stable_core],
            default=0.0,
        )
        stable_core_edge_rejected_max = max(
            [float(item["edge_rejected_ratio"]) for item in stable_core],
            default=0.0,
        )
        use_compensated_limits = motion_packet and bool(compensated)
        use_stable_core_limits = motion_packet and bool(stable_core)
        limit_mean = max_mean
        limit_changed = max_changed
        limit_large = max_large
        limit_prefix = "max_"
        if use_compensated_limits:
            limit_mean = max_compensated_mean
            limit_changed = max_compensated_changed
            limit_large = max_compensated_large
            limit_prefix = "max_motion_compensated_"
        if use_stable_core_limits:
            limit_mean = max_stable_core_mean
            limit_changed = max_stable_core_changed
            limit_large = max_stable_core_large
            limit_prefix = "max_motion_stable_core_"
        warn_mean_limit = (
            warn_motion_compensated_mean_abs_luma_delta
            if use_compensated_limits
            else warn_mean_abs_luma_delta
        )
        warn_changed_limit = (
            warn_motion_compensated_changed_pixel_ratio
            if use_compensated_limits
            else warn_changed_pixel_ratio
        )
        warn_large_limit = (
            warn_motion_compensated_large_changed_pixel_ratio
            if use_compensated_limits
            else warn_large_changed_pixel_ratio
        )

        if hard_gate_view and limit_mean > max_mean_abs_luma_delta:
            row_failures.append(
                f"{limit_prefix}mean_abs_luma_delta {limit_mean:.6f} > {max_mean_abs_luma_delta:.6f}"
            )
        elif limit_mean > warn_mean_limit:
            row_warnings.append(
                f"{limit_prefix}mean_abs_luma_delta {limit_mean:.6f} > {warn_mean_limit:.6f}"
            )

        if hard_gate_view and limit_changed > max_changed_pixel_ratio:
            row_failures.append(
                f"{limit_prefix}changed_pixel_ratio {limit_changed:.6f} > {max_changed_pixel_ratio:.6f}"
            )
        elif limit_changed > warn_changed_limit:
            row_warnings.append(
                f"{limit_prefix}changed_pixel_ratio {limit_changed:.6f} > {warn_changed_limit:.6f}"
            )

        if hard_gate_view and limit_large > max_large_changed_pixel_ratio:
            row_failures.append(
                f"{limit_prefix}large_changed_pixel_ratio {limit_large:.6f} > {max_large_changed_pixel_ratio:.6f}"
            )
        elif limit_large > warn_large_limit:
            row_warnings.append(
                f"{limit_prefix}large_changed_pixel_ratio {limit_large:.6f} > {warn_large_limit:.6f}"
            )

        for failure in row_failures:
            failures.append(f"{family}:{view}:{failure}")
        if informational_view and row_warnings:
            row_diagnostic_signals.extend(row_warnings)
            row_warnings = []

        for warning in row_warnings:
            warnings.append(f"{family}:{view}:{warning}")
        for signal in row_diagnostic_signals:
            diagnostic_signals.append(f"{family}:{view}:{signal}")

        reports.append(
            {
                "family": family,
                "view": view,
                "debug_view": row.get("debug_view"),
                "profile_id": (row.get("scene_visual_contract") or {}).get("profile_id"),
                "stability_motion_mode": str(row.get("stability_motion_mode") or motion_mode),
                "informational_view": informational_view,
                "motion_frames": row.get("motion_frames"),
                "motion_look_amplitude": row.get("motion_look_amplitude"),
                "motion_side_amplitude": row.get("motion_side_amplitude"),
                "motion_forward_amplitude": row.get("motion_forward_amplitude"),
                "motion_lift_amplitude": row.get("motion_lift_amplitude"),
                "motion_look_cycles": row.get("motion_look_cycles"),
                "fixed_delta_time": row.get("fixed_delta_time"),
                "hard_gate_view": hard_gate_view,
                "capture_count": len(sequence),
                "comparison_count": len(valid_comparisons),
                "max_mean_abs_luma_delta": max_mean,
                "max_changed_pixel_ratio": max_changed,
                "max_large_changed_pixel_ratio": max_large,
                "max_motion_compensated_mean_abs_luma_delta": max_compensated_mean,
                "max_motion_compensated_changed_pixel_ratio": max_compensated_changed,
                "max_motion_compensated_large_changed_pixel_ratio": max_compensated_large,
                "motion_compensated_comparison_count": len(compensated),
                "motion_compensated_min_overlap_ratio": compensated_overlap_min,
                "motion_compensated_limits_used": use_compensated_limits,
                "max_motion_stable_core_mean_abs_luma_delta": max_stable_core_mean,
                "max_motion_stable_core_changed_pixel_ratio": max_stable_core_changed,
                "max_motion_stable_core_large_changed_pixel_ratio": max_stable_core_large,
                "motion_stable_core_comparison_count": len(stable_core),
                "motion_stable_core_min_ratio": stable_core_ratio_min,
                "motion_stable_core_max_edge_rejected_ratio": stable_core_edge_rejected_max,
                "motion_stable_core_limits_used": use_stable_core_limits,
                "comparisons": comparisons,
                "status": "FAIL" if row_failures else "PASS",
                "failures": row_failures,
                "warnings": row_warnings,
                "diagnostic_signals": row_diagnostic_signals,
            }
        )

    report = {
        "schema": "cortex.scene_local_cinematic_renderer_v1.packet_stability_analysis",
        "manifest": str(manifest_path),
        "stability_motion_mode": motion_mode,
        "motion": {
            "motion_frames": manifest.get("motion_frames"),
            "motion_look_amplitude": manifest.get("motion_look_amplitude"),
            "motion_side_amplitude": manifest.get("motion_side_amplitude"),
            "motion_forward_amplitude": manifest.get("motion_forward_amplitude"),
            "motion_lift_amplitude": manifest.get("motion_lift_amplitude"),
            "motion_look_cycles": manifest.get("motion_look_cycles"),
            "fixed_delta_time": manifest.get("fixed_delta_time"),
        },
        "thresholds": {
            "max_mean_abs_luma_delta": max_mean_abs_luma_delta,
            "max_changed_pixel_ratio": max_changed_pixel_ratio,
            "max_large_changed_pixel_ratio": max_large_changed_pixel_ratio,
            "warn_mean_abs_luma_delta": warn_mean_abs_luma_delta,
            "warn_changed_pixel_ratio": warn_changed_pixel_ratio,
            "warn_large_changed_pixel_ratio": warn_large_changed_pixel_ratio,
            "warn_motion_compensated_mean_abs_luma_delta": warn_motion_compensated_mean_abs_luma_delta,
            "warn_motion_compensated_changed_pixel_ratio": warn_motion_compensated_changed_pixel_ratio,
            "warn_motion_compensated_large_changed_pixel_ratio": warn_motion_compensated_large_changed_pixel_ratio,
            "changed_threshold": changed_threshold,
            "large_changed_threshold": large_changed_threshold,
            "max_dimension": max_dimension,
            "alignment_max_dimension": alignment_max_dimension,
            "max_alignment_shift": max_alignment_shift,
            "edge_threshold": edge_threshold,
            "edge_dilation": edge_dilation,
            "min_stable_core_ratio": min_stable_core_ratio,
        },
        "status": "FAIL" if failures else "PASS",
        "failure_count": len(failures),
        "warning_count": len(warnings),
        "diagnostic_signal_count": len(diagnostic_signals),
        "hard_gate_warning_count": sum(
            1
            for item in reports
            if item["hard_gate_view"]
            for _ in item["warnings"]
        ),
        "diagnostic_warning_count": sum(
            1
            for item in reports
            if not item["hard_gate_view"]
            for _ in item["warnings"]
        ),
        "failures": failures,
        "warnings": warnings,
        "diagnostic_signals": diagnostic_signals,
        "result_count": len(reports),
        "results": reports,
        "summary": [
            {
                "family": item["family"],
                "view": item["view"],
                "status": item["status"],
                "stability_motion_mode": item["stability_motion_mode"],
                "hard_gate_view": item["hard_gate_view"],
                "informational_view": item["informational_view"],
                "capture_count": item["capture_count"],
                "comparison_count": item["comparison_count"],
                "max_mean_abs_luma_delta": item["max_mean_abs_luma_delta"],
                "max_changed_pixel_ratio": item["max_changed_pixel_ratio"],
                "max_large_changed_pixel_ratio": item["max_large_changed_pixel_ratio"],
                "max_motion_compensated_mean_abs_luma_delta": item["max_motion_compensated_mean_abs_luma_delta"],
                "max_motion_compensated_changed_pixel_ratio": item["max_motion_compensated_changed_pixel_ratio"],
                "max_motion_compensated_large_changed_pixel_ratio": item["max_motion_compensated_large_changed_pixel_ratio"],
                "motion_compensated_limits_used": item["motion_compensated_limits_used"],
                "max_motion_stable_core_mean_abs_luma_delta": item["max_motion_stable_core_mean_abs_luma_delta"],
                "max_motion_stable_core_changed_pixel_ratio": item["max_motion_stable_core_changed_pixel_ratio"],
                "max_motion_stable_core_large_changed_pixel_ratio": item["max_motion_stable_core_large_changed_pixel_ratio"],
                "motion_stable_core_min_ratio": item["motion_stable_core_min_ratio"],
                "motion_stable_core_max_edge_rejected_ratio": item["motion_stable_core_max_edge_rejected_ratio"],
                "motion_stable_core_limits_used": item["motion_stable_core_limits_used"],
                "warnings": item["warnings"],
                "diagnostic_signals": item["diagnostic_signals"],
            }
            for item in reports
        ],
        "aggregate": {
            "max_mean_abs_luma_delta": max(
                [float(item["max_mean_abs_luma_delta"]) for item in reports],
                default=0.0,
            ),
            "max_changed_pixel_ratio": max(
                [float(item["max_changed_pixel_ratio"]) for item in reports],
                default=0.0,
            ),
            "max_large_changed_pixel_ratio": max(
                [float(item["max_large_changed_pixel_ratio"]) for item in reports],
                default=0.0,
            ),
            "max_motion_compensated_mean_abs_luma_delta": max(
                [float(item["max_motion_compensated_mean_abs_luma_delta"]) for item in reports],
                default=0.0,
            ),
            "max_motion_compensated_changed_pixel_ratio": max(
                [float(item["max_motion_compensated_changed_pixel_ratio"]) for item in reports],
                default=0.0,
            ),
            "max_motion_compensated_large_changed_pixel_ratio": max(
                [float(item["max_motion_compensated_large_changed_pixel_ratio"]) for item in reports],
                default=0.0,
            ),
            "max_motion_stable_core_mean_abs_luma_delta": max(
                [float(item["max_motion_stable_core_mean_abs_luma_delta"]) for item in reports],
                default=0.0,
            ),
            "max_motion_stable_core_changed_pixel_ratio": max(
                [float(item["max_motion_stable_core_changed_pixel_ratio"]) for item in reports],
                default=0.0,
            ),
            "max_motion_stable_core_large_changed_pixel_ratio": max(
                [float(item["max_motion_stable_core_large_changed_pixel_ratio"]) for item in reports],
                default=0.0,
            ),
        },
        "hard_gate_aggregate": {
            "max_mean_abs_luma_delta": max(
                [float(item["max_mean_abs_luma_delta"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_changed_pixel_ratio": max(
                [float(item["max_changed_pixel_ratio"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_large_changed_pixel_ratio": max(
                [float(item["max_large_changed_pixel_ratio"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_motion_compensated_mean_abs_luma_delta": max(
                [float(item["max_motion_compensated_mean_abs_luma_delta"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_motion_compensated_changed_pixel_ratio": max(
                [float(item["max_motion_compensated_changed_pixel_ratio"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_motion_compensated_large_changed_pixel_ratio": max(
                [float(item["max_motion_compensated_large_changed_pixel_ratio"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_motion_stable_core_mean_abs_luma_delta": max(
                [float(item["max_motion_stable_core_mean_abs_luma_delta"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_motion_stable_core_changed_pixel_ratio": max(
                [float(item["max_motion_stable_core_changed_pixel_ratio"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
            "max_motion_stable_core_large_changed_pixel_ratio": max(
                [float(item["max_motion_stable_core_large_changed_pixel_ratio"]) for item in reports if item["hard_gate_view"]],
                default=0.0,
            ),
        },
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--write-manifest", action="store_true")
    parser.add_argument("--max-mean-abs-luma-delta", type=float, default=24.0)
    parser.add_argument("--max-changed-pixel-ratio", type=float, default=0.45)
    parser.add_argument("--max-large-changed-pixel-ratio", type=float, default=0.18)
    parser.add_argument("--warn-mean-abs-luma-delta", type=float, default=4.0)
    parser.add_argument("--warn-changed-pixel-ratio", type=float, default=0.08)
    parser.add_argument("--warn-large-changed-pixel-ratio", type=float, default=0.03)
    parser.add_argument("--warn-motion-compensated-mean-abs-luma-delta", type=float, default=6.0)
    parser.add_argument("--warn-motion-compensated-changed-pixel-ratio", type=float, default=0.18)
    parser.add_argument("--warn-motion-compensated-large-changed-pixel-ratio", type=float, default=0.03)
    parser.add_argument("--changed-threshold", type=float, default=10.0)
    parser.add_argument("--large-changed-threshold", type=float, default=35.0)
    parser.add_argument("--max-dimension", type=int, default=384)
    parser.add_argument("--alignment-max-dimension", type=int, default=64)
    parser.add_argument("--max-alignment-shift", type=int, default=8)
    parser.add_argument("--edge-threshold", type=float, default=18.0)
    parser.add_argument("--edge-dilation", type=int, default=1)
    parser.add_argument("--min-stable-core-ratio", type=float, default=0.35)
    args = parser.parse_args()

    manifest_path = args.manifest.resolve()
    report = analyze_manifest(
        manifest_path,
        max_mean_abs_luma_delta=args.max_mean_abs_luma_delta,
        max_changed_pixel_ratio=args.max_changed_pixel_ratio,
        max_large_changed_pixel_ratio=args.max_large_changed_pixel_ratio,
        warn_mean_abs_luma_delta=args.warn_mean_abs_luma_delta,
        warn_changed_pixel_ratio=args.warn_changed_pixel_ratio,
        warn_large_changed_pixel_ratio=args.warn_large_changed_pixel_ratio,
        warn_motion_compensated_mean_abs_luma_delta=args.warn_motion_compensated_mean_abs_luma_delta,
        warn_motion_compensated_changed_pixel_ratio=args.warn_motion_compensated_changed_pixel_ratio,
        warn_motion_compensated_large_changed_pixel_ratio=args.warn_motion_compensated_large_changed_pixel_ratio,
        changed_threshold=args.changed_threshold,
        large_changed_threshold=args.large_changed_threshold,
        max_dimension=args.max_dimension,
        alignment_max_dimension=args.alignment_max_dimension,
        max_alignment_shift=args.max_alignment_shift,
        edge_threshold=args.edge_threshold,
        edge_dilation=args.edge_dilation,
        min_stable_core_ratio=args.min_stable_core_ratio,
    )

    out_path = args.out or (manifest_path.parent / "packet_stability_analysis.json")
    write_json(out_path, report)

    if args.write_manifest:
        manifest = load_json(manifest_path)
        manifest["packet_stability_analysis"] = {
            "status": report["status"],
            "report": str(out_path),
            "failure_count": report["failure_count"],
            "warning_count": report["warning_count"],
            "diagnostic_signal_count": report["diagnostic_signal_count"],
            "hard_gate_warning_count": report["hard_gate_warning_count"],
            "diagnostic_warning_count": report["diagnostic_warning_count"],
            "diagnostic_signals": report["diagnostic_signals"],
            "aggregate": report["aggregate"],
            "hard_gate_aggregate": report["hard_gate_aggregate"],
            "summary": report["summary"],
        }
        write_json(manifest_path, manifest)

    print(json.dumps({
        "status": report["status"],
        "manifest": str(manifest_path),
        "report": str(out_path),
        "failure_count": report["failure_count"],
        "warning_count": report["warning_count"],
        "diagnostic_signal_count": report["diagnostic_signal_count"],
        "hard_gate_warning_count": report["hard_gate_warning_count"],
        "diagnostic_warning_count": report["diagnostic_warning_count"],
        "result_count": report["result_count"],
        "aggregate": report["aggregate"],
        "hard_gate_aggregate": report["hard_gate_aggregate"],
    }, indent=2))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
