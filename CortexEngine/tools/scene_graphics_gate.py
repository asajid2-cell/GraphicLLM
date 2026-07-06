#!/usr/bin/env python3
"""Render-health gate for generated exterior stills.

This file used to be a large graphics-fidelity gate with many hard pixel
proxies. That created the wrong incentive: runtime overlay passes were added to
feed metrics instead of making scenes cleaner. The reset gate keeps only hard
render-health checks and per-render frame-pipeline evidence. Image statistics
and old graphics contracts are emitted as telemetry for review, not as pass/fail
proof that a scene is AAA.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
from pathlib import Path
from typing import Any

try:
    from PIL import Image
except Exception:  # pragma: no cover - checked at runtime
    Image = None


ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "build" / "bin" / "logs"


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")[:56] or "scene"


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _read_log(path: Path | None) -> str:
    candidates: list[Path] = []
    if path:
        candidates.append(path)
    candidates.append(LOGS / "cortex_last_run.txt")
    for candidate in candidates:
        try:
            if candidate.exists():
                return candidate.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue
    return ""


def _frame_report_candidates(frame_report: Path | None, ir: Path | None, png: Path | None, log: Path | None) -> list[Path]:
    candidates: list[Path] = []
    if frame_report:
        candidates.append(frame_report)
    for path in (png, ir, log):
        if not path:
            continue
        stem = path.stem
        if stem.endswith("_ir"):
            stem = stem[:-3]
        elif stem.endswith("_frame_report"):
            stem = stem[: -len("_frame_report")]
        candidates.append(path.with_name(f"{stem}_frame_report.json"))

    deduped: list[Path] = []
    seen: set[str] = set()
    for candidate in candidates:
        key = str(candidate.resolve()) if candidate.exists() else str(candidate)
        if key not in seen:
            deduped.append(candidate)
            seen.add(key)
    return deduped


def _load_frame_report(frame_report: Path | None, ir: Path | None, png: Path | None, log: Path | None) -> dict[str, Any] | None:
    for candidate in _frame_report_candidates(frame_report, ir, png, log):
        try:
            if candidate.exists():
                data = _load_json(candidate)
                if isinstance(data, dict):
                    data["_gate_source_path"] = str(candidate)
                    return data
        except Exception:
            continue
    return None


def _string_set(values: Any) -> set[str]:
    if isinstance(values, list):
        return {str(v) for v in values}
    return set()


def _pass_by_name(frame_contract: dict[str, Any], name: str) -> dict[str, Any]:
    for item in frame_contract.get("passes") or []:
        if isinstance(item, dict) and item.get("name") == name:
            return item
    return {}


def _resource_names(frame_contract: dict[str, Any]) -> set[str]:
    names: set[str] = set()
    for item in frame_contract.get("resources") or []:
        if isinstance(item, dict) and item.get("name") and item.get("valid"):
            names.add(str(item["name"]))
    return names


def _frame_pipeline_health(frame_report: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(frame_report, dict):
        return {
            "ok": False,
            "report_present": False,
            "problems": ["frame_report_sidecar_absent"],
        }

    frame_contract = frame_report.get("frame_contract") or {}
    if not isinstance(frame_contract, dict):
        return {
            "ok": False,
            "report_present": True,
            "path": frame_report.get("_gate_source_path"),
            "problems": ["frame_contract_absent"],
        }

    features = frame_contract.get("features") or {}
    executed_features = frame_contract.get("executed_features") or {}
    culling = frame_contract.get("culling") or {}
    draw_counts = frame_contract.get("draw_counts") or {}
    v3 = frame_contract.get("full_scene_shader_pipeline_v3") or {}

    visibility = _pass_by_name(frame_contract, "VisibilityBuffer")
    material = _pass_by_name(frame_contract, "VBMaterialResolve")
    lighting = _pass_by_name(frame_contract, "VBDeferredLighting")
    resources = _resource_names(frame_contract)

    try:
        vb_instances = int(draw_counts.get("visibility_buffer_instances", 0) or 0)
        vb_materials = int(draw_counts.get("visibility_buffer_materials", 0) or 0)
        vb_batches = int(draw_counts.get("visibility_buffer_draw_batches", 0) or 0)
    except Exception:
        vb_instances = vb_materials = vb_batches = 0

    material_writes = _string_set(material.get("writes"))
    lighting_reads = _string_set(lighting.get("reads"))
    lighting_writes = _string_set(lighting.get("writes"))
    visibility_writes = _string_set(visibility.get("writes"))
    required_resources = {
        "visibility_buffer",
        "vb_gbuffer_albedo",
        "vb_gbuffer_normal_roughness",
        "vb_gbuffer_material_ext2",
        "shadow_map",
    }

    telemetry = {
        "ok": True,
        "report_present": True,
        "path": frame_report.get("_gate_source_path"),
        "visibility_buffer_enabled": bool(features.get("visibility_buffer_enabled")),
        "visibility_buffer_executed_feature": bool(executed_features.get("visibility_buffer_enabled")),
        "visibility_buffer_rendered": bool(culling.get("visibility_buffer_rendered")),
        "visibility_buffer_instances": vb_instances,
        "visibility_buffer_materials": vb_materials,
        "visibility_buffer_draw_batches": vb_batches,
        "visibility_pass_executed": bool(visibility.get("executed")),
        "visibility_pass_writes": sorted(visibility_writes),
        "material_resolve_executed": bool(material.get("executed")),
        "material_resolve_writes": sorted(material_writes),
        "deferred_lighting_executed": bool(lighting.get("executed")),
        "deferred_lighting_reads": sorted(lighting_reads),
        "deferred_lighting_writes": sorted(lighting_writes),
        "valid_resources": sorted(resources & required_resources),
        "material_attributes_ready": bool(v3.get("material_attributes_ready")),
        "lighting_adapter_ready": bool(v3.get("lighting_adapter_ready")),
        "render_graph_v3_inventory_ready": bool(v3.get("render_graph_v3_inventory_ready")),
    }

    problems: list[str] = []
    if not telemetry["visibility_buffer_enabled"]:
        problems.append("visibility_buffer_disabled")
    if not telemetry["visibility_buffer_executed_feature"]:
        problems.append("visibility_buffer_feature_not_executed")
    if not telemetry["visibility_buffer_rendered"]:
        problems.append("visibility_buffer_not_rendered")
    if vb_instances <= 0 or vb_materials <= 0 or vb_batches <= 0:
        problems.append("visibility_buffer_draw_counts_empty")
    if not telemetry["visibility_pass_executed"]:
        problems.append("visibility_pass_not_executed")
    if not {"visibility_buffer", "vb_gbuffer_albedo", "vb_gbuffer_normal_roughness", "vb_gbuffer_material_ext2"}.issubset(visibility_writes):
        problems.append("visibility_pass_gbuffer_writes_absent")
    if not telemetry["material_resolve_executed"]:
        problems.append("material_resolve_not_executed")
    if not {"gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_material_ext2"}.issubset(material_writes):
        problems.append("material_resolve_writes_absent")
    if not telemetry["deferred_lighting_executed"]:
        problems.append("deferred_lighting_not_executed")
    if not {"gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_material_ext2", "shadow_map"}.issubset(lighting_reads):
        problems.append("deferred_lighting_reads_absent")
    if "hdr_color" not in lighting_writes:
        problems.append("deferred_lighting_hdr_write_absent")
    if not required_resources.issubset(resources):
        problems.append("required_frame_resources_absent")
    if not telemetry["material_attributes_ready"]:
        problems.append("material_attributes_not_ready")
    if not telemetry["lighting_adapter_ready"]:
        problems.append("lighting_adapter_not_ready")

    telemetry["problems"] = problems
    telemetry["ok"] = not problems
    return telemetry


def _image_metrics(path: Path | None) -> tuple[dict[str, Any] | None, list[str]]:
    problems: list[str] = []
    if not path:
        return None, ["png_argument_absent"]
    if not path.exists():
        return None, ["png_file_absent"]
    if Image is None:
        return None, ["pillow_unavailable"]

    try:
        with Image.open(path) as image:
            rgb = image.convert("RGB")
            width, height = rgb.size
            if width <= 0 or height <= 0:
                return {"width": width, "height": height}, ["png_dimensions_invalid"]

            sample = rgb.resize((min(width, 320), min(height, 180)))
            pixels = list(sample.getdata())
    except Exception as exc:
        return None, [f"png_decode_failed:{exc}"]

    count = max(1, len(pixels))
    luma_sum = 0.0
    nonblack = 0
    near_white = 0
    saturated = 0
    edge_sum = 0.0
    sample_w, sample_h = sample.size
    lumas: list[float] = []
    for r, g, b in pixels:
        luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
        lumas.append(luma)
        luma_sum += luma
        if max(r, g, b) > 8:
            nonblack += 1
        if min(r, g, b) > 238:
            near_white += 1
        if max(r, g, b) - min(r, g, b) > 96:
            saturated += 1

    for y in range(1, sample_h - 1):
        row = y * sample_w
        for x in range(1, sample_w - 1):
            center = lumas[row + x]
            gx = lumas[row + x + 1] - lumas[row + x - 1]
            gy = lumas[row + sample_w + x] - lumas[row - sample_w + x]
            edge_sum += abs(center) * 0.0 + math.sqrt(gx * gx + gy * gy)

    avg_luma = luma_sum / count
    nonblack_fraction = nonblack / count
    near_white_fraction = near_white / count
    saturated_fraction = saturated / count
    edge_density = edge_sum / max(1, (sample_w - 2) * (sample_h - 2))

    metrics = {
        "path": str(path),
        "width": width,
        "height": height,
        "sample_width": sample_w,
        "sample_height": sample_h,
        "avg_luma": round(avg_luma, 4),
        "nonblack_fraction": round(nonblack_fraction, 4),
        "near_white_fraction": round(near_white_fraction, 4),
        "saturated_fraction": round(saturated_fraction, 4),
        "edge_density": round(edge_density, 4),
    }

    if width < 256 or height < 144:
        problems.append("png_too_small")
    if nonblack_fraction < 0.35:
        problems.append("mostly_black_frame")
    if avg_luma < 0.015:
        problems.append("underlit_or_blank_frame")
    if near_white_fraction > 0.90:
        problems.append("mostly_white_frame")
    if not all(math.isfinite(float(v)) for v in (avg_luma, nonblack_fraction, near_white_fraction, saturated_fraction, edge_density)):
        problems.append("image_metric_not_finite")
    return metrics, problems


def _legacy_telemetry(ir: dict[str, Any], log_text: str) -> dict[str, Any]:
    graphics_pass = ir.get("graphics_pass") or {}
    if not isinstance(graphics_pass, dict):
        graphics_pass = {}
    director = ir.get("director") or {}
    if not isinstance(director, dict):
        director = {}

    receipt_names = [
        "generative_exterior:",
        "visibility buffer",
        "VBMaterialResolve",
        "VBDeferredLighting",
        "water",
        "terrain",
        "occlusion",
    ]
    return {
        "graphics_pass_keys": sorted(str(key) for key in graphics_pass.keys()),
        "graphics_pass_count": len(graphics_pass),
        "director_scene_type": director.get("scene_type"),
        "runtime_receipt_hits": {
            name: log_text.count(name)
            for name in receipt_names
        },
    }


def evaluate(prompt: str, ir: dict[str, Any], png: Path | None, log_text: str, frame_report: dict[str, Any] | None) -> dict[str, Any]:
    failures: list[dict[str, Any]] = []
    warnings: list[dict[str, Any]] = []

    def fail(code: str, message: str, **detail: Any) -> None:
        failures.append({"code": code, "message": message, "detail": detail})

    image, image_problems = _image_metrics(png)
    frame_pipeline = _frame_pipeline_health(frame_report)
    legacy = _legacy_telemetry(ir, log_text)

    if image is None:
        fail("render_health_image_unavailable", "PNG could not be loaded for render-health checks", problems=image_problems)
    elif image_problems:
        fail("render_health_image", "PNG failed basic render-health checks", image=image, problems=image_problems)

    if not frame_pipeline.get("ok"):
        fail(
            "render_health_frame_pipeline",
            "Per-render frame report does not prove the visibility-buffer/deferred lighting path",
            frame_pipeline=frame_pipeline,
        )

    if legacy["graphics_pass_count"] > 0:
        warnings.append(
            {
                "code": "legacy_graphics_contracts_telemetry_only",
                "message": "IR graphics_pass contracts are reported as telemetry, not hard visual-quality proof",
            }
        )

    return {
        "prompt": prompt,
        "passed": not failures,
        "failures": failures,
        "warnings": warnings,
        "metrics": {
            "image": image,
            "frame_pipeline": frame_pipeline,
            "legacy_telemetry": legacy,
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Generated-scene render-health gate")
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--ir", required=True, type=Path)
    ap.add_argument("--png", type=Path)
    ap.add_argument("--log", type=Path)
    ap.add_argument("--frame-report", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--expect-fail", action="store_true")
    args = ap.parse_args()

    ir = _load_json(args.ir)
    log_text = _read_log(args.log)
    frame_report = _load_frame_report(args.frame_report, args.ir, args.png, args.log)
    report = evaluate(args.prompt, ir, args.png, log_text, frame_report)

    out_dir = args.out or (LOGS / "scene_graphics" / _slug(args.prompt))
    out_dir.mkdir(parents=True, exist_ok=True)
    report_path = out_dir / "graphics_gate_report.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    print(f"graphics report: {report_path}")

    if args.expect_fail:
        if report["passed"]:
            print("expected render-health failure, but gate passed", file=sys.stderr)
            return 2
        return 0
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
