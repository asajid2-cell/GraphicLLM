#!/usr/bin/env python3
"""Build a visual review sheet from a Full Scene Shader V2 packet manifest."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont


DEFAULT_VIEWS = [
    "beauty",
    "reflection_resolver_candidate",
    "reflection_resolver_candidate_delta",
    "reflection_source_authority",
    "reflection_source_weights",
]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def first_existing(paths: list[str]) -> Path | None:
    for item in paths:
        if not item:
            continue
        path = Path(item)
        if path.exists():
            return path
    return None


def collect_rows(manifest: dict[str, Any], views: list[str]) -> list[dict[str, Any]]:
    by_family: dict[str, dict[str, dict[str, Any]]] = {}
    for result in manifest.get("results", []):
        family = str(result.get("family", ""))
        view = str(result.get("view", ""))
        if not family or view not in views:
            continue
        by_family.setdefault(family, {})[view] = result

    rows: list[dict[str, Any]] = []
    for family in sorted(by_family):
        view_map = by_family[family]
        cells: list[dict[str, Any]] = []
        for view in views:
            result = view_map.get(view)
            capture = None
            if result:
                capture = first_existing(
                    [str(result.get("capture", ""))]
                    + [str(item) for item in result.get("capture_sequence", [])]
                )
            cells.append({"view": view, "capture": capture})
        rows.append(
            {
                "family": family,
                "scene": str(next(iter(view_map.values())).get("scene", "")) if view_map else "",
                "camera_bookmark": str(next(iter(view_map.values())).get("camera_bookmark", "")) if view_map else "",
                "cells": cells,
            }
        )
    return rows


def load_font(size: int) -> ImageFont.ImageFont:
    for name in ("arial.ttf", "segoeui.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def fit_image(path: Path, width: int, height: int) -> Image.Image:
    image = Image.open(path).convert("RGB")
    image.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (width, height), (18, 20, 24))
    x = (width - image.width) // 2
    y = (height - image.height) // 2
    canvas.paste(image, (x, y))
    return canvas


def draw_missing(width: int, height: int, text: str, font: ImageFont.ImageFont) -> Image.Image:
    image = Image.new("RGB", (width, height), (42, 28, 30))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, width - 1, height - 1), outline=(130, 65, 70), width=2)
    draw.text((16, 16), text, fill=(245, 200, 200), font=font)
    return image


def build_sheet(
    rows: list[dict[str, Any]],
    views: list[str],
    output: Path,
    *,
    thumb_width: int,
    thumb_height: int,
) -> dict[str, Any]:
    title_font = load_font(20)
    label_font = load_font(15)
    small_font = load_font(12)
    margin = 18
    label_h = 28
    row_label_h = 42
    gutter = 8

    width = margin * 2 + len(views) * thumb_width + (len(views) - 1) * gutter
    height = margin * 2 + label_h + len(rows) * (row_label_h + thumb_height + margin)
    sheet = Image.new("RGB", (width, height), (12, 14, 18))
    draw = ImageDraw.Draw(sheet)

    title = "Full Scene Shader V2 Stress Review"
    draw.text((margin, 8), title, fill=(235, 238, 244), font=title_font)
    y = margin + label_h
    for col, view in enumerate(views):
        x = margin + col * (thumb_width + gutter)
        draw.text((x, y - 20), view, fill=(185, 196, 210), font=small_font)

    missing: list[str] = []
    for row in rows:
        draw.text(
            (margin, y + 4),
            f"{row['family']}  scene={row['scene']}  bookmark={row['camera_bookmark']}",
            fill=(230, 235, 242),
            font=label_font,
        )
        y += row_label_h
        for col, cell in enumerate(row["cells"]):
            x = margin + col * (thumb_width + gutter)
            capture = cell["capture"]
            if capture:
                thumb = fit_image(capture, thumb_width, thumb_height)
            else:
                missing.append(f"{row['family']}/{cell['view']}")
                thumb = draw_missing(thumb_width, thumb_height, "missing", label_font)
            sheet.paste(thumb, (x, y))
        y += thumb_height + margin

    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)
    return {
        "schema": "cortex.full_scene_shader_pipeline_v2.review_sheet.v1",
        "output": str(output),
        "row_count": len(rows),
        "views": views,
        "missing": missing,
        "width": width,
        "height": height,
    }


def write_markdown(summary: dict[str, Any], path: Path, manifest_path: Path) -> None:
    lines = [
        "# Full Scene Shader V2 Stress Review Sheet",
        "",
        f"- manifest: `{manifest_path}`",
        f"- image: `{summary['output']}`",
        f"- rows: {summary['row_count']}",
        f"- views: {', '.join(summary['views'])}",
        f"- missing cells: {len(summary['missing'])}",
    ]
    if summary["missing"]:
        lines.extend(["", "## Missing", ""])
        lines.extend(f"- {item}" for item in summary["missing"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--summary-json", type=Path)
    parser.add_argument("--summary-md", type=Path)
    parser.add_argument("--views", default=",".join(DEFAULT_VIEWS))
    parser.add_argument("--thumb-width", type=int, default=360)
    parser.add_argument("--thumb-height", type=int, default=210)
    args = parser.parse_args()

    views = [item.strip() for item in args.views.split(",") if item.strip()]
    if not views:
        raise SystemExit("at least one view is required")

    manifest = load_json(args.manifest)
    rows = collect_rows(manifest, views)
    summary = build_sheet(
        rows,
        views,
        args.output,
        thumb_width=args.thumb_width,
        thumb_height=args.thumb_height,
    )
    summary["manifest"] = str(args.manifest)

    summary_json = args.summary_json or args.output.with_suffix(".json")
    summary_md = args.summary_md or args.output.with_suffix(".md")
    summary_json.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    write_markdown(summary, summary_md, args.manifest)

    if summary["missing"]:
        for item in summary["missing"]:
            print(f"WARN: missing review cell {item}")
    print(f"review_sheet={args.output}")
    print(f"summary_json={summary_json}")
    print(f"summary_md={summary_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
