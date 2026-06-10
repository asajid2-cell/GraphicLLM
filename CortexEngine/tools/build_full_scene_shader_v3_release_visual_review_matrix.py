#!/usr/bin/env python3
"""Build a release visual-artifact review matrix for FullSceneShaderPipeline V3.

Tool marker: build_full_scene_shader_v3_release_visual_review_matrix.py.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Any

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REQUIRED_FAMILIES = [
    "stress_rt_showcase_reflection_closeup",
    "gallery",
    "kitchen",
    "office",
    "gym",
    "concert",
    "red_room",
    "stadium",
]
DEFAULT_REQUIRED_MOTIONS = ["static", "mouse_jitter", "camera_sweep", "light_sweep"]
DEFAULT_REQUIRED_VIEWS = [
    "beauty",
    "candidate_beauty_v3",
    "scene_local_environment",
    "material_family",
    "reflection_source_id",
    "v3_shadow_source_attribution",
    "v3_lighting_energy_budget",
]
CONTACT_SHEET_VIEWS = [
    "beauty",
    "candidate_beauty_v3",
    "scene_local_environment",
    "reflection_source_id",
    "v3_shadow_source_attribution",
]


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def normalize_path(raw: str) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path
    for candidate in (Path.cwd() / path, ROOT / path):
        if candidate.exists():
            return candidate.resolve()
    return (ROOT / path).resolve()


def first_existing(paths: list[Any], base: Path) -> Path | None:
    for raw in paths:
        if not raw:
            continue
        path = Path(str(raw))
        if not path.is_absolute():
            path = base / path
        if path.exists():
            return path.resolve()
    return None


def image_metrics(path: Path, *, max_width: int = 256) -> dict[str, Any]:
    with Image.open(path) as image:
        rgb = image.convert("RGB")
        original_width, original_height = rgb.size
        if rgb.width > max_width:
            height = max(1, int(round(rgb.height * (max_width / float(rgb.width)))))
            rgb = rgb.resize((max_width, height), Image.Resampling.BILINEAR)
        width, height = rgb.size
        raw = rgb.tobytes()

    luma: list[float] = []
    for i in range(0, len(raw), 3):
        r = raw[i] / 255.0
        g = raw[i + 1] / 255.0
        b = raw[i + 2] / 255.0
        luma.append(0.2126 * r + 0.7152 * g + 0.0722 * b)
    count = max(1, len(luma))
    mean = sum(luma) / count
    std = math.sqrt(sum((value - mean) ** 2 for value in luma) / count)
    dark_ratio = sum(1 for value in luma if value < 0.02) / count
    bright_ratio = sum(1 for value in luma if value > 0.98) / count
    return {
        "image": str(path),
        "original_width": original_width,
        "original_height": original_height,
        "sample_width": width,
        "sample_height": height,
        "mean_luma": mean,
        "luma_std": std,
        "dark_ratio": dark_ratio,
        "bright_ratio": bright_ratio,
        "nonblank": original_width > 0 and original_height > 0 and std > 0.001,
    }


def manifest_family_view_rows(manifest: dict[str, Any]) -> dict[str, dict[str, list[dict[str, Any]]]]:
    by_family: dict[str, dict[str, list[dict[str, Any]]]] = {}
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        family = str(row.get("family", ""))
        view = str(row.get("view", ""))
        if family and view:
            by_family.setdefault(family, {}).setdefault(view, []).append(row)
    return by_family


def promoted_packet_roots(default_promotion_matrix: Path) -> list[Path]:
    matrix = load_json(default_promotion_matrix)
    roots: list[Path] = []
    for packet in matrix.get("packets", []):
        if not isinstance(packet, dict):
            continue
        if int(packet.get("promoted_report_count", 0) or 0) <= 0:
            continue
        raw = str(packet.get("packet_root") or "")
        if raw:
            roots.append(normalize_path(raw))
    return roots


def packet_row(packet_root: Path, required_views: list[str]) -> dict[str, Any]:
    manifest_path = packet_root / "manifest.json"
    visual_quality_path = packet_root / "visual_quality_analysis.json"
    row: dict[str, Any] = {
        "packet_root": str(packet_root),
        "manifest": str(manifest_path),
        "exists": packet_root.exists(),
        "motion_mode": "unknown",
        "capture_sequence_count": 0,
        "families": [],
        "required_views": required_views,
        "artifact_ready": False,
        "review_cell_count": 0,
        "nonblank_beauty_count": 0,
        "visual_quality_status": None,
        "family_rows": [],
        "failures": [],
        "warnings": [],
    }
    if not packet_root.exists():
        row["failures"].append("packet root missing")
        return row
    if not manifest_path.exists():
        row["failures"].append("manifest.json missing")
        return row

    manifest = load_json(manifest_path)
    base = manifest_path.parent
    family_views = manifest_family_view_rows(manifest)
    row["families"] = sorted(family_views)
    row["motion_mode"] = str(manifest.get("stability_motion_mode", "static"))
    row["capture_sequence_count"] = int(manifest.get("capture_sequence_count", 0) or 0)

    if visual_quality_path.exists():
        visual_quality = load_json(visual_quality_path)
        row["visual_quality_status"] = str(visual_quality.get("status", ""))
        if visual_quality.get("release_gate") == "FAIL":
            row["warnings"].append("legacy visual_quality_analysis release gate failed")
        elif visual_quality.get("release_gate") == "REVIEW_REQUIRED":
            row["warnings"].append("visual_quality_analysis requires human review")
    else:
        row["warnings"].append("visual_quality_analysis.json missing")

    for family in sorted(family_views):
        views = family_views[family]
        family_row: dict[str, Any] = {
            "family": family,
            "views": {},
            "missing_views": [],
            "failures": [],
            "warnings": [],
        }
        for view in required_views:
            candidates = views.get(view, [])
            capture = None
            if candidates:
                first = candidates[0]
                capture = first_existing(
                    [first.get("capture")] + list(first.get("capture_sequence", [])),
                    base,
                )
            if capture is None:
                family_row["missing_views"].append(view)
                family_row["failures"].append(f"{view}: capture missing")
                continue
            metrics = image_metrics(capture)
            view_row = {
                "capture": str(capture),
                "metrics": metrics,
                "reviewable": metrics["original_width"] >= 64 and metrics["original_height"] >= 64,
            }
            if not view_row["reviewable"]:
                family_row["failures"].append(f"{view}: capture below reviewable size")
            if view in ("beauty", "candidate_beauty_v3") and not metrics["nonblank"]:
                family_row["failures"].append(f"{view}: nonblank image sanity failed")
            if view == "beauty" and metrics["nonblank"]:
                row["nonblank_beauty_count"] += 1
            row["review_cell_count"] += 1
            family_row["views"][view] = view_row
        for failure in family_row["failures"]:
            row["failures"].append(f"{family}: {failure}")
        for warning in family_row["warnings"]:
            row["warnings"].append(f"{family}: {warning}")
        row["family_rows"].append(family_row)

    row["artifact_ready"] = not row["failures"] and row["review_cell_count"] > 0
    return row


def load_font(size: int) -> ImageFont.ImageFont:
    for name in ("segoeui.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def fit_image(path: Path, width: int, height: int) -> Image.Image:
    image = Image.open(path).convert("RGB")
    image.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (width, height), (16, 18, 22))
    canvas.paste(image, ((width - image.width) // 2, (height - image.height) // 2))
    return canvas


def draw_missing(width: int, height: int, text: str, font: ImageFont.ImageFont) -> Image.Image:
    image = Image.new("RGB", (width, height), (48, 28, 32))
    draw = ImageDraw.Draw(image)
    draw.rectangle((0, 0, width - 1, height - 1), outline=(140, 70, 78), width=2)
    draw.text((12, 12), text, fill=(245, 210, 210), font=font)
    return image


def build_contact_sheet(
    rows: list[dict[str, Any]],
    output: Path,
    *,
    views: list[str],
    thumb_width: int,
    thumb_height: int,
) -> dict[str, Any]:
    cells: list[dict[str, Any]] = []
    for row in rows:
        for family_row in row.get("family_rows", []):
            cells.append(
                {
                    "family": family_row["family"],
                    "motion": row["motion_mode"],
                    "packet_root": row["packet_root"],
                    "views": family_row.get("views", {}),
                }
            )

    title_font = load_font(18)
    label_font = load_font(13)
    small_font = load_font(11)
    margin = 16
    gutter = 8
    header_height = 54
    row_label_height = 34
    width = margin * 2 + len(views) * thumb_width + max(0, len(views) - 1) * gutter
    height = margin * 2 + header_height + len(cells) * (row_label_height + thumb_height + margin)
    sheet = Image.new("RGB", (width, height), (10, 12, 16))
    draw = ImageDraw.Draw(sheet)
    draw.text((margin, 10), "Full Scene Shader V3 Release Visual Review", fill=(235, 238, 244), font=title_font)
    for index, view in enumerate(views):
        x = margin + index * (thumb_width + gutter)
        draw.text((x, margin + 30), view, fill=(180, 190, 205), font=small_font)

    y = margin + header_height
    missing_cells: list[str] = []
    for cell in cells:
        draw.text(
            (margin, y),
            f"{cell['family']}  motion={cell['motion']}",
            fill=(232, 236, 244),
            font=label_font,
        )
        y += row_label_height
        for index, view in enumerate(views):
            x = margin + index * (thumb_width + gutter)
            view_info = cell["views"].get(view)
            if view_info and Path(view_info["capture"]).exists():
                thumb = fit_image(Path(view_info["capture"]), thumb_width, thumb_height)
            else:
                missing_cells.append(f"{cell['family']}/{cell['motion']}/{view}")
                thumb = draw_missing(thumb_width, thumb_height, "missing", label_font)
            sheet.paste(thumb, (x, y))
        y += thumb_height + margin

    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)
    return {
        "contact_sheet": str(output),
        "contact_sheet_exists": output.exists(),
        "contact_sheet_width": width,
        "contact_sheet_height": height,
        "contact_sheet_cell_count": len(cells) * len(views),
        "contact_sheet_missing_cells": missing_cells,
    }


def build_matrix(
    packet_roots: list[Path],
    *,
    required_families: list[str],
    required_motion_modes: list[str],
    required_views: list[str],
    contact_sheet: Path,
) -> dict[str, Any]:
    rows = [packet_row(root, required_views) for root in packet_roots]
    ready_rows = [row for row in rows if row.get("artifact_ready") is True]
    observed_families = sorted(
        {
            family
            for row in ready_rows
            for family in row.get("families", [])
            if isinstance(family, str) and family
        }
    )
    observed_motion_modes = sorted(
        {
            str(row.get("motion_mode"))
            for row in ready_rows
            if row.get("motion_mode") not in (None, "", "unknown")
        }
    )
    failures: list[str] = []
    warnings: list[str] = []
    for row in rows:
        for failure in row.get("failures", []):
            failures.append(f"{row.get('packet_root')}: {failure}")
        for warning in row.get("warnings", []):
            warnings.append(f"{row.get('packet_root')}: {warning}")

    missing_families = sorted(set(required_families) - set(observed_families))
    missing_motion_modes = sorted(set(required_motion_modes) - set(observed_motion_modes))
    if missing_families:
        failures.append("missing release visual families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing release visual motion modes: " + ", ".join(missing_motion_modes))

    sheet = build_contact_sheet(
        rows,
        contact_sheet,
        views=[view for view in CONTACT_SHEET_VIEWS if view in required_views],
        thumb_width=320,
        thumb_height=180,
    )
    if not sheet["contact_sheet_exists"] or sheet["contact_sheet_cell_count"] <= 0:
        failures.append("contact sheet was not generated")
    if sheet["contact_sheet_missing_cells"]:
        failures.append("contact sheet has missing cells")

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.release_visual_review_matrix.v1",
        "packet_count": len(rows),
        "artifact_ready_packet_count": len(ready_rows),
        "required_families": required_families,
        "observed_families": observed_families,
        "missing_families": missing_families,
        "required_motion_modes": required_motion_modes,
        "observed_motion_modes": observed_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "required_views": required_views,
        "review_cell_count": sum(int(row.get("review_cell_count", 0) or 0) for row in rows),
        "nonblank_beauty_count": sum(int(row.get("nonblank_beauty_count", 0) or 0) for row in rows),
        "contact_sheet": sheet,
        "human_visual_acceptance_required": True,
        "visual_artifact_review_ready": not failures,
        "human_review_packet_ready": not failures and sheet["contact_sheet_exists"],
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(matrix: dict[str, Any], output: Path) -> None:
    lines = [
        "# Full Scene Shader V3 Release Visual Review Matrix",
        "",
        f"- visual artifact review ready: `{str(matrix['visual_artifact_review_ready']).lower()}`",
        f"- human review packet ready: `{str(matrix['human_review_packet_ready']).lower()}`",
        f"- human visual acceptance required: `{str(matrix['human_visual_acceptance_required']).lower()}`",
        f"- packet count: `{matrix['packet_count']}`",
        f"- artifact-ready packets: `{matrix['artifact_ready_packet_count']}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- required views: `{', '.join(matrix['required_views'])}`",
        f"- review cells: `{matrix['review_cell_count']}`",
        f"- nonblank beauty captures: `{matrix['nonblank_beauty_count']}`",
        f"- contact sheet: `{matrix['contact_sheet']['contact_sheet']}`",
        f"- failures: `{len(matrix['failures'])}`",
        f"- warnings: `{len(matrix['warnings'])}`",
        "",
        "| Packet | Motion | Families | Ready | Review Cells | Nonblank Beauty | Failures | Warnings |",
        "|---|---|---|---:|---:|---:|---:|---:|",
    ]
    for row in matrix["rows"]:
        lines.append(
            "| {packet} | {motion} | {families} | {ready} | {cells} | {beauty} | {failures} | {warnings} |".format(
                packet=row["packet_root"],
                motion=row["motion_mode"],
                families=", ".join(row.get("families", [])),
                ready=str(bool(row.get("artifact_ready"))).lower(),
                cells=row.get("review_cell_count", 0),
                beauty=row.get("nonblank_beauty_count", 0),
                failures=len(row.get("failures", [])),
                warnings=len(row.get("warnings", [])),
            )
        )
    if matrix["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in matrix["warnings"])
    if matrix["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in matrix["failures"])
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--default-promotion-matrix", required=True, type=Path)
    parser.add_argument("--packet-root", action="append", default=[])
    parser.add_argument("--required-families", default=",".join(DEFAULT_REQUIRED_FAMILIES))
    parser.add_argument("--required-motion-modes", default=",".join(DEFAULT_REQUIRED_MOTIONS))
    parser.add_argument("--required-views", default=",".join(DEFAULT_REQUIRED_VIEWS))
    parser.add_argument("--contact-sheet", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    packet_roots = [normalize_path(raw) for raw in args.packet_root]
    if not packet_roots:
        packet_roots = promoted_packet_roots(args.default_promotion_matrix)

    matrix = build_matrix(
        packet_roots,
        required_families=split_csv(args.required_families),
        required_motion_modes=split_csv(args.required_motion_modes),
        required_views=split_csv(args.required_views),
        contact_sheet=args.contact_sheet,
    )
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")
    write_markdown(matrix, args.output_md)

    print(f"release_visual_review_matrix={args.output_json}")
    print(f"release_visual_review_matrix_md={args.output_md}")
    print(f"contact_sheet={matrix['contact_sheet']['contact_sheet']}")
    if not matrix["visual_artifact_review_ready"]:
        for failure in matrix["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: FullSceneShaderPipeline V3 release visual artifacts are review-ready")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
