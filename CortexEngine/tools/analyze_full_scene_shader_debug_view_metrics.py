#!/usr/bin/env python3
"""Measure Full Scene Shader V2 packet debug-view captures.

The packet already proves that debug views exist. This tool makes those views
comparable across runs by extracting simple image statistics from the captured
BMPs without depending on Pillow or engine-side frame reports.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path
from typing import Any


def _read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _read_i32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<i", data, offset)[0]


def read_bmp_rgb(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if len(data) < 54 or data[0:2] != b"BM":
        raise ValueError(f"not a BMP file: {path}")

    pixel_offset = _read_u32(data, 10)
    dib_size = _read_u32(data, 14)
    if dib_size < 40:
        raise ValueError(f"unsupported BMP DIB header: {path}")

    width = _read_i32(data, 18)
    height_raw = _read_i32(data, 22)
    planes = _read_u16(data, 26)
    bits_per_pixel = _read_u16(data, 28)
    compression = _read_u32(data, 30)

    if planes != 1 or compression != 0 or bits_per_pixel not in (24, 32):
        raise ValueError(
            f"unsupported BMP format for {path}: planes={planes} bpp={bits_per_pixel} compression={compression}"
        )
    if width <= 0 or height_raw == 0:
        raise ValueError(f"invalid BMP dimensions for {path}: {width}x{height_raw}")

    top_down = height_raw < 0
    height = abs(height_raw)
    bytes_per_pixel = bits_per_pixel // 8
    row_stride = ((width * bytes_per_pixel + 3) // 4) * 4
    needed = pixel_offset + row_stride * height
    if needed > len(data):
        raise ValueError(f"truncated BMP pixel data for {path}")

    pixels: list[tuple[int, int, int]] = []
    for row_index in range(height):
        src_row = row_index if top_down else (height - 1 - row_index)
        row_base = pixel_offset + src_row * row_stride
        for x in range(width):
            base = row_base + x * bytes_per_pixel
            b, g, r = data[base], data[base + 1], data[base + 2]
            pixels.append((r, g, b))
    return width, height, pixels


def measure_capture(path: Path) -> dict[str, Any]:
    width, height, pixels = read_bmp_rgb(path)
    count = max(len(pixels), 1)
    sum_r = sum(p[0] for p in pixels)
    sum_g = sum(p[1] for p in pixels)
    sum_b = sum(p[2] for p in pixels)
    max_r = max((p[0] for p in pixels), default=0)
    max_g = max((p[1] for p in pixels), default=0)
    max_b = max((p[2] for p in pixels), default=0)
    nonblack = sum(1 for r, g, b in pixels if max(r, g, b) > 3)
    hot = sum(1 for r, g, b in pixels if max(r, g, b) > 245)
    luma_values = [0.2126 * r + 0.7152 * g + 0.0722 * b for r, g, b in pixels]
    mean_luma = sum(luma_values) / count
    max_luma = max(luma_values, default=0.0)

    return {
        "width": width,
        "height": height,
        "pixel_count": count,
        "mean_rgb": [sum_r / count / 255.0, sum_g / count / 255.0, sum_b / count / 255.0],
        "max_rgb": [max_r / 255.0, max_g / 255.0, max_b / 255.0],
        "mean_luma": mean_luma / 255.0,
        "max_luma": max_luma / 255.0,
        "nonblack_ratio": nonblack / count,
        "hot_pixel_ratio": hot / count,
    }


def build_report(manifest_path: Path) -> dict[str, Any]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    rows: list[dict[str, Any]] = []
    failures: list[str] = []

    for result in manifest.get("results", []):
        capture = result.get("capture")
        if not capture:
            failures.append(f"missing capture path for {result.get('family')}/{result.get('view')}")
            continue
        capture_path = Path(capture)
        if not capture_path.exists():
            failures.append(f"capture file does not exist: {capture_path}")
            continue
        try:
            metrics = measure_capture(capture_path)
        except Exception as exc:  # noqa: BLE001 - include artifact path in report.
            failures.append(f"failed to measure {capture_path}: {exc}")
            continue
        rows.append(
            {
                "family": result.get("family", ""),
                "view": result.get("view", ""),
                "debug_view": result.get("debug_view"),
                "scene": result.get("scene", ""),
                "capture": str(capture_path),
                "metrics": metrics,
            }
        )

    return {
        "schema": "cortex.full_scene_shader_pipeline_v2.debug_view_metrics.v1",
        "manifest": str(manifest_path),
        "output_root": manifest.get("output_root", ""),
        "captured_view_count": len(manifest.get("results", [])),
        "measured_view_count": len(rows),
        "failures": failures,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines = [
        "# Full Scene Shader Pipeline V2 Debug View Metrics",
        "",
        f"- manifest: `{report['manifest']}`",
        f"- captured views: {report['captured_view_count']}",
        f"- measured views: {report['measured_view_count']}",
        f"- failures: {len(report['failures'])}",
        "",
        "| Family | View | Debug | Mean RGB | Mean Luma | Nonblack | Hot Pixels |",
        "|---|---|---:|---:|---:|---:|---:|",
    ]
    for row in report["rows"]:
        metrics = row["metrics"]
        mean_rgb = ",".join(f"{v:.4f}" for v in metrics["mean_rgb"])
        lines.append(
            "| {family} | {view} | {debug} | {mean_rgb} | {mean_luma:.4f} | {nonblack:.4f} | {hot:.4f} |".format(
                family=row["family"],
                view=row["view"],
                debug="" if row["debug_view"] is None else row["debug_view"],
                mean_rgb=mean_rgb,
                mean_luma=metrics["mean_luma"],
                nonblack=metrics["nonblack_ratio"],
                hot=metrics["hot_pixel_ratio"],
            )
        )
    if report["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--output-md", type=Path)
    args = parser.parse_args()

    report = build_report(args.manifest)
    output_json = args.output_json or args.manifest.with_name("debug_view_metrics.json")
    output_md = args.output_md or args.manifest.with_name("debug_view_metrics.md")
    output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, output_md)

    if report["failures"]:
        for failure in report["failures"]:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1

    print(f"PASS: measured {report['measured_view_count']} debug-view captures")
    print(f"json={output_json}")
    print(f"markdown={output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
