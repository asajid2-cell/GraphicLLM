#!/usr/bin/env python3
"""Measure RT Showcase reported wall/floor flicker in fixed screen ROIs.

The existing full-frame mouse-jitter smoke is intentionally strict, but the
reported repro contains a visible HDRI office background. Small camera yaw moves
that background and all high-contrast object silhouettes, so full-frame deltas
can be dominated by expected parallax. This analyzer keeps that signal, but
also reports the wall/floor/pool regions separately.

Optional aligned debug-view captures can be supplied as masks. A foreground
mask from VB normal/roughness or depth debug views lets the report distinguish
opaque receiver motion from depth-miss HDRI/background motion in the same screen
ROI.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple


Roi = Tuple[int, int, int, int]

DEFAULT_ROIS: Dict[str, Roi] = {
    "white_platform": (410, 382, 1120, 570),
    "white_platform_clean_right": (835, 500, 1165, 585),
    "white_platform_clean_left": (35, 60, 475, 185),
    "front_dark_floor": (430, 560, 1130, 715),
    "front_dark_floor_clean": (500, 612, 1090, 705),
    "left_wall_panel": (205, 220, 535, 445),
    "left_wall_panel_clean": (230, 245, 500, 425),
    "pool_water_rim": (650, 375, 930, 470),
    "background_office": (710, 105, 1265, 365),
    "whole_frame": (0, 0, 1280, 720),
}


def read_bmp(path: Path) -> Tuple[int, int, int, bytes, int, int]:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError(f"{path} is not an uncompressed BMP")
    offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    height_signed = struct.unpack_from("<i", data, 22)[0]
    planes = struct.unpack_from("<H", data, 26)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or height_signed == 0 or planes != 1 or bpp not in (24, 32) or compression != 0:
        raise ValueError(f"{path} uses unsupported BMP layout")
    height = abs(height_signed)
    bytes_per_pixel = bpp // 8
    stride = ((width * bytes_per_pixel + 3) // 4) * 4
    if offset + stride * height > len(data):
        raise ValueError(f"{path} is truncated")
    return width, height, bytes_per_pixel, data, offset, stride


Bmp = Tuple[int, int, int, bytes, int, int]


def pixel_rgb(image: Bmp, x: int, y: int) -> Tuple[int, int, int]:
    width, height, bpp, data, offset, stride = image
    row = height - 1 - y
    p = offset + row * stride + x * bpp
    b = data[p + 0]
    g = data[p + 1]
    r = data[p + 2]
    return r, g, b


def pixel_luma(image: Bmp, x: int, y: int) -> float:
    r, g, b = pixel_rgb(image, x, y)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def pixel_luma_delta(a: Bmp,
                     b: Bmp,
                     x: int,
                     y: int) -> float:
    width, height, bpp, data_a, offset_a, stride_a = a
    _, _, _, data_b, offset_b, stride_b = b
    # BMP rows in this renderer are bottom-up; both frames share orientation, so
    # use the same address transform for each.
    row = height - 1 - y
    pa = offset_a + row * stride_a + x * bpp
    pb = offset_b + row * stride_b + x * bpp
    db = abs(data_a[pa + 0] - data_b[pb + 0])
    dg = abs(data_a[pa + 1] - data_b[pb + 1])
    dr = abs(data_a[pa + 2] - data_b[pb + 2])
    return 0.2126 * dr + 0.7152 * dg + 0.0722 * db


def mask_accepts(mask: Optional[Bmp],
                 x: int,
                 y: int,
                 threshold: float,
                 invert: bool,
                 mode: str,
                 reference_rgb: Tuple[int, int, int]) -> bool:
    if mask is None:
        return True
    if mode == "not-reference-color":
        r, g, b = pixel_rgb(mask, x, y)
        rr, rg, rb = reference_rgb
        foreground = max(abs(r - rr), abs(g - rg), abs(b - rb)) > threshold
    else:
        value = pixel_luma(mask, x, y)
        foreground = value > threshold
    return (not foreground) if invert else foreground


def measure_roi(a: Bmp,
                b: Bmp,
                roi: Roi,
                mask: Optional[Bmp] = None,
                mask_threshold: float = 8.0,
                invert_mask: bool = False,
                mask_mode: str = "luma",
                reference_rgb: Tuple[int, int, int] = (0, 0, 0)) -> Dict[str, float]:
    width, height = a[0], a[1]
    x0, y0, x1, y1 = roi
    x0 = max(0, min(width, x0))
    x1 = max(0, min(width, x1))
    y0 = max(0, min(height, y0))
    y1 = max(0, min(height, y1))
    if x1 <= x0 or y1 <= y0:
        raise ValueError(f"empty ROI {roi}")
    total = 0.0
    changed = 0
    large = 0
    max_delta = 0.0
    count = 0
    rejected = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            if not mask_accepts(mask, x, y, mask_threshold, invert_mask, mask_mode, reference_rgb):
                rejected += 1
                continue
            delta = pixel_luma_delta(a, b, x, y)
            total += delta
            max_delta = max(max_delta, delta)
            if delta > 10.0:
                changed += 1
            if delta > 35.0:
                large += 1
            count += 1
    if count <= 0:
        return {
            "mean_abs_luma_delta": 0.0,
            "changed_pixel_ratio": 0.0,
            "large_changed_pixel_ratio": 0.0,
            "max_luma_delta": 0.0,
            "pixel_count": 0,
            "masked_out_pixel_count": rejected,
            "mask_coverage": 0.0,
        }
    roi_pixel_count = (x1 - x0) * (y1 - y0)
    return {
        "mean_abs_luma_delta": total / count,
        "changed_pixel_ratio": changed / count,
        "large_changed_pixel_ratio": large / count,
        "max_luma_delta": max_delta,
        "pixel_count": count,
        "masked_out_pixel_count": rejected,
        "mask_coverage": count / roi_pixel_count,
    }


def frame_number(path: Path) -> int:
    stem = path.stem
    return int(stem.rsplit("_", 1)[-1])


def iter_pairs(captures: List[Path]) -> Iterable[Tuple[Path, Path]]:
    for left, right in zip(captures, captures[1:]):
        yield left, right


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture-dir", required=True, type=Path)
    parser.add_argument("--mask-dir", type=Path,
                        help="Optional aligned debug-view BMP capture directory used as a foreground/background mask")
    parser.add_argument("--mask-threshold", type=float, default=8.0,
                        help="Luma threshold above which mask pixels are considered foreground")
    parser.add_argument("--mask-mode", choices=("luma", "not-reference-color"), default="luma",
                        help="How to classify mask pixels as foreground")
    parser.add_argument("--mask-reference-x", type=int, default=1270,
                        help="Reference pixel X for not-reference-color mode")
    parser.add_argument("--mask-reference-y", type=int, default=10,
                        help="Reference pixel Y for not-reference-color mode")
    parser.add_argument("--invert-mask", action="store_true",
                        help="Measure pixels rejected by the foreground mask instead of accepted pixels")
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    captures = sorted(args.capture_dir.glob("visual_validation_frame_*.bmp"), key=frame_number)
    if len(captures) < 2:
        raise SystemExit(f"need at least two captures in {args.capture_dir}")

    mask_by_frame: Dict[int, Path] = {}
    if args.mask_dir:
        mask_captures = sorted(args.mask_dir.glob("visual_validation_frame_*.bmp"), key=frame_number)
        mask_by_frame = {frame_number(path): path for path in mask_captures}
        missing = [path.name for path in captures if frame_number(path) not in mask_by_frame]
        if missing:
            raise SystemExit(
                f"mask dir {args.mask_dir} is missing {len(missing)} aligned frames, first={missing[0]}")

    rows = []
    aggregate = {
        name: {
            "max_mean_abs_luma_delta": 0.0,
            "max_changed_pixel_ratio": 0.0,
            "max_large_changed_pixel_ratio": 0.0,
            "max_mask_coverage": 0.0,
        }
        for name in DEFAULT_ROIS
    }
    for left, right in iter_pairs(captures):
        a = read_bmp(left)
        b = read_bmp(right)
        if a[:3] != b[:3]:
            raise SystemExit(f"capture dimensions differ: {left.name} {right.name}")
        mask = None
        if mask_by_frame:
            mask = read_bmp(mask_by_frame[frame_number(left)])
            if mask[:3] != a[:3]:
                raise SystemExit(f"mask dimensions differ for {left.name}")
        reference_rgb = (0, 0, 0)
        if mask is not None and args.mask_mode == "not-reference-color":
            ref_x = max(0, min(mask[0] - 1, args.mask_reference_x))
            ref_y = max(0, min(mask[1] - 1, args.mask_reference_y))
            reference_rgb = pixel_rgb(mask, ref_x, ref_y)
        roi_rows = {}
        for name, roi in DEFAULT_ROIS.items():
            stats = measure_roi(
                a,
                b,
                roi,
                mask=mask,
                mask_threshold=args.mask_threshold,
                invert_mask=args.invert_mask,
                mask_mode=args.mask_mode,
                reference_rgb=reference_rgb)
            roi_rows[name] = stats
            aggregate[name]["max_mean_abs_luma_delta"] = max(
                aggregate[name]["max_mean_abs_luma_delta"],
                stats["mean_abs_luma_delta"])
            aggregate[name]["max_changed_pixel_ratio"] = max(
                aggregate[name]["max_changed_pixel_ratio"],
                stats["changed_pixel_ratio"])
            aggregate[name]["max_large_changed_pixel_ratio"] = max(
                aggregate[name]["max_large_changed_pixel_ratio"],
                stats["large_changed_pixel_ratio"])
            aggregate[name]["max_mask_coverage"] = max(
                aggregate[name]["max_mask_coverage"],
                stats["mask_coverage"])
        rows.append({"from": left.name, "to": right.name, "rois": roi_rows})

    result = {
        "schema": "cortex.rt_showcase.wall_floor_roi_stability.v1",
        "capture_dir": str(args.capture_dir),
        "capture_count": len(captures),
        "mask_dir": str(args.mask_dir) if args.mask_dir else "",
        "mask_threshold": args.mask_threshold,
        "mask_mode": args.mask_mode,
        "invert_mask": args.invert_mask,
        "pair_count": len(rows),
        "rois": {name: list(roi) for name, roi in DEFAULT_ROIS.items()},
        "aggregate": aggregate,
        "pairs": rows,
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(result, indent=2), encoding="utf-8")

    lines = [
        "# RT Showcase Wall/Floor ROI Stability",
        "",
        f"Capture dir: `{args.capture_dir}`",
        "",
        "| ROI | Max mean luma | Max changed | Max large changed | Max mask coverage |",
        "|---|---:|---:|---:|---:|",
    ]
    for name, stats in aggregate.items():
        lines.append(
            f"| {name} | {stats['max_mean_abs_luma_delta']:.4f} | "
            f"{stats['max_changed_pixel_ratio']:.4f} | "
            f"{stats['max_large_changed_pixel_ratio']:.4f} | "
            f"{stats.get('max_mask_coverage', 1.0):.4f} |")
    args.output_md.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
