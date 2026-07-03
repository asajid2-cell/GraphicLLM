#!/usr/bin/env python3
"""Graphics-fidelity gate for generated exterior stills.

This complements scene_quality_gate.py. It does not claim an image is AAA; it
rejects the obvious blockout class: flat generated exteriors with disconnected
props, no terrain/contact/material/shader pass, weak occlusion layering, weak
surface material breakup, weak texture-backed material coverage, weak
source-backed environment assets, and no runtime evidence that the high-quality
exterior graphics path ran.
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
except Exception:  # pragma: no cover
    Image = None


ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "build" / "bin" / "logs"


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")[:56] or "scene"


def _load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def _prompt_flags(prompt: str) -> dict[str, bool]:
    p = prompt.lower()
    return {
        "exterior": any(w in p for w in ("lake", "river", "mountain", "campsite", "camp", "canyon", "alpine", "desert", "forest")),
        "water": any(w in p for w in ("lake", "river", "water", "shore")),
        "campsite": any(w in p for w in ("camp", "campsite")),
        "canyon": "canyon" in p,
        "desert": "desert" in p,
        "moonlight": any(w in p for w in ("moon", "moonlight", "night")),
    }


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


def _executed_pass(passes: dict[str, dict[str, Any]], name: str) -> dict[str, Any]:
    value = passes.get(name)
    return value if isinstance(value, dict) else {}


def _full_scene_pipeline_evidence(frame_report: dict[str, Any] | None) -> dict[str, Any]:
    if not isinstance(frame_report, dict):
        return {
            "ok": False,
            "report_present": False,
            "missing": ["frame_report_sidecar"],
        }

    frame_contract = frame_report.get("frame_contract") or {}
    if not isinstance(frame_contract, dict):
        return {
            "ok": False,
            "report_present": True,
            "path": frame_report.get("_gate_source_path"),
            "missing": ["frame_contract"],
        }

    features = frame_contract.get("features") or {}
    executed_features = frame_contract.get("executed_features") or {}
    culling = frame_contract.get("culling") or {}
    draw_counts = frame_contract.get("draw_counts") or {}
    v3 = frame_contract.get("full_scene_shader_pipeline_v3") or {}

    pass_records = frame_contract.get("passes") or []
    passes = {
        str(p.get("name")): p
        for p in pass_records
        if isinstance(p, dict) and p.get("name")
    }
    resources = {
        str(r.get("name")): r
        for r in (frame_contract.get("resources") or [])
        if isinstance(r, dict) and r.get("name")
    }

    required_resources = {
        "visibility_buffer",
        "vb_gbuffer_albedo",
        "vb_gbuffer_normal_roughness",
        "vb_gbuffer_material_ext2",
        "shadow_map",
    }
    valid_resources = {
        name
        for name in required_resources
        if isinstance(resources.get(name), dict) and bool(resources[name].get("valid"))
    }

    vb_material_resolve = _executed_pass(passes, "VBMaterialResolve")
    vb_deferred_lighting = _executed_pass(passes, "VBDeferredLighting")
    visibility_buffer = _executed_pass(passes, "VisibilityBuffer")
    vb_material_writes = _string_set(vb_material_resolve.get("writes"))
    vb_deferred_reads = _string_set(vb_deferred_lighting.get("reads"))
    vb_deferred_writes = _string_set(vb_deferred_lighting.get("writes"))
    visibility_writes = _string_set(visibility_buffer.get("writes"))
    gpu_passes = frame_report.get("gpu_passes") or []
    gpu_visibility_ms = 0.0
    gpu_visibility_seen = False
    for gpu_pass in gpu_passes:
        if isinstance(gpu_pass, dict) and gpu_pass.get("name") == "VisibilityBuffer":
            gpu_visibility_seen = True
            try:
                gpu_visibility_ms = max(gpu_visibility_ms, float(gpu_pass.get("ms", 0.0) or 0.0))
            except Exception:
                pass

    try:
        vb_instances = int(draw_counts.get("visibility_buffer_instances", 0) or 0)
        vb_materials = int(draw_counts.get("visibility_buffer_materials", 0) or 0)
        vb_batches = int(draw_counts.get("visibility_buffer_draw_batches", 0) or 0)
    except Exception:
        vb_instances = vb_materials = vb_batches = 0

    v3_pass_names = _string_set(v3.get("render_graph_v3_pass_names"))
    evidence = {
        "ok": True,
        "report_present": True,
        "path": frame_report.get("_gate_source_path"),
        "visibility_buffer_enabled": bool(features.get("visibility_buffer_enabled")),
        "visibility_buffer_executed": bool(executed_features.get("visibility_buffer_enabled")),
        "visibility_buffer_rendered": bool(culling.get("visibility_buffer_rendered")),
        "visibility_buffer_instances": vb_instances,
        "visibility_buffer_materials": vb_materials,
        "visibility_buffer_draw_batches": vb_batches,
        "visibility_buffer_gpu_pass_seen": gpu_visibility_seen,
        "visibility_buffer_gpu_ms": round(gpu_visibility_ms, 4),
        "vb_material_resolve_executed": bool(vb_material_resolve.get("executed")),
        "vb_material_resolve_writes": sorted(vb_material_writes),
        "vb_deferred_lighting_executed": bool(vb_deferred_lighting.get("executed")),
        "vb_deferred_lighting_reads": sorted(vb_deferred_reads),
        "vb_deferred_lighting_writes": sorted(vb_deferred_writes),
        "visibility_buffer_pass_executed": bool(visibility_buffer.get("executed")),
        "visibility_buffer_pass_writes": sorted(visibility_writes),
        "valid_resources": sorted(valid_resources),
        "material_attributes_ready": bool(v3.get("material_attributes_ready")),
        "lighting_adapter_ready": bool(v3.get("lighting_adapter_ready")),
        "lighting_split_resources_allocated": bool(v3.get("lighting_split_resources_allocated")),
        "lighting_split_resources_ready": bool(v3.get("lighting_split_resources_ready")),
        "render_graph_v3_inventory_ready": bool(v3.get("render_graph_v3_inventory_ready")),
        "render_graph_v3_pass_names": sorted(v3_pass_names),
    }

    missing: list[str] = []
    if not evidence["visibility_buffer_enabled"]:
        missing.append("visibility_buffer_enabled")
    if not evidence["visibility_buffer_executed"]:
        missing.append("visibility_buffer_executed_feature")
    if not evidence["visibility_buffer_rendered"]:
        missing.append("visibility_buffer_rendered")
    if vb_instances <= 0:
        missing.append("visibility_buffer_instances")
    if vb_materials <= 0:
        missing.append("visibility_buffer_materials")
    if vb_batches <= 0:
        missing.append("visibility_buffer_draw_batches")
    if not gpu_visibility_seen:
        missing.append("visibility_buffer_gpu_pass")
    if not evidence["visibility_buffer_pass_executed"]:
        missing.append("VisibilityBuffer_pass")
    if not {"visibility_buffer", "vb_gbuffer_albedo", "vb_gbuffer_normal_roughness", "vb_gbuffer_material_ext2"}.issubset(visibility_writes):
        missing.append("VisibilityBuffer_gbuffer_writes")
    if not evidence["vb_material_resolve_executed"]:
        missing.append("VBMaterialResolve_pass")
    if not {"gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_material_ext2"}.issubset(vb_material_writes):
        missing.append("VBMaterialResolve_gbuffer_writes")
    if not evidence["vb_deferred_lighting_executed"]:
        missing.append("VBDeferredLighting_pass")
    if not {"gbuffer_albedo", "gbuffer_normal_roughness", "gbuffer_material_ext2", "shadow_map"}.issubset(vb_deferred_reads):
        missing.append("VBDeferredLighting_material_shadow_reads")
    if "hdr_color" not in vb_deferred_writes:
        missing.append("VBDeferredLighting_hdr_write")
    if valid_resources != required_resources:
        missing.append("valid_visibility_gbuffer_resources")
    if not evidence["material_attributes_ready"]:
        missing.append("full_scene_v3_material_attributes_ready")
    if not evidence["lighting_adapter_ready"]:
        missing.append("full_scene_v3_lighting_adapter_ready")
    if not evidence["render_graph_v3_inventory_ready"]:
        missing.append("full_scene_v3_render_graph_inventory")
    if not {"VisibilityBuffer", "VBDeferredLighting"}.issubset(v3_pass_names):
        missing.append("full_scene_v3_pass_inventory")

    evidence["missing"] = missing
    evidence["ok"] = not missing
    return evidence


def _objects(ir: dict[str, Any]) -> list[dict[str, Any]]:
    return [o for o in ir.get("objects") or [] if isinstance(o, dict)]


def _ground(ir: dict[str, Any]) -> dict[str, Any]:
    return ((ir.get("environment") or {}).get("ground") or {}) if isinstance(ir, dict) else {}


def _graphics(ir: dict[str, Any]) -> dict[str, Any]:
    env = ir.get("environment") or {}
    direct = env.get("graphics_pass")
    if isinstance(direct, dict):
        return direct
    director = ir.get("director") or {}
    nested = director.get("graphics_pass")
    return nested if isinstance(nested, dict) else {}


def _material_detail_count(ir: dict[str, Any]) -> int:
    count = 0
    for obj in _objects(ir):
        mat = obj.get("material") or {}
        if not isinstance(mat, dict):
            continue
        richness = 0
        for key in (
            "preset",
            "roughness",
            "normal_scale",
            "procedural_mask",
            "wetness",
            "specular",
            "ao",
            "clearcoat",
            "sheen",
            "subsurface",
            "anisotropy",
        ):
            if key in mat:
                richness += 1
        if richness >= 3:
            count += 1
    return count


def _advanced_material_count(ir: dict[str, Any]) -> int:
    count = 0
    advanced_keys = {"ao", "clearcoat", "sheen", "subsurface", "anisotropy"}
    for obj in _objects(ir):
        mat = obj.get("material") or {}
        if not isinstance(mat, dict):
            continue
        present = 0
        for key in advanced_keys:
            try:
                if float(mat.get(key, 0.0) or 0.0) > 0.001:
                    present += 1
            except Exception:
                continue
        if present >= 2:
            count += 1
    return count


def _material_zone_count(zones: Any) -> int:
    if isinstance(zones, dict):
        declared = zones.get("count")
        try:
            if declared is not None:
                return int(declared)
        except Exception:
            pass
        names = zones.get("zones")
        if isinstance(names, list):
            return len([z for z in names if z])
        return len([k for k, v in zones.items() if k not in {"count", "zones"} and v])
    if isinstance(zones, list):
        return len([z for z in zones if z])
    return 0


def _texture_runtime_counts(log_text: str) -> dict[str, int] | None:
    m = re.search(
        r"generative_exterior: texture material fidelity "
        r"terrain=(\d+) rock=(\d+) wood=(\d+) fabric=(\d+) hero=(\d+) shore=(\d+) texture_sets=(\d+)",
        log_text,
    )
    if not m:
        return None
    keys = ("terrain", "rock", "wood", "fabric", "hero", "shore", "texture_sets")
    return {key: int(value) for key, value in zip(keys, m.groups())}


def _renderer_shadow_runtime(log_text: str) -> dict[str, Any] | None:
    m = re.search(
        r"generative_exterior: renderer shadow occlusion budget "
        r"ssao=(on|off) shadows=(on|off) "
        r"ssao_radius=([0-9.]+) ssao_bias=([0-9.]+) ssao_intensity=([0-9.]+) "
        r"shadow_bias=([0-9.]+) shadow_pcf=([0-9.]+) "
        r"contact_patches=(\d+) soft_penumbra=(\d+) "
        r"overlay_budget=(\d+) dxr_required=(0|1)",
        log_text,
    )
    if not m:
        return None
    return {
        "ssao": m.group(1) == "on",
        "shadows": m.group(2) == "on",
        "ssao_radius": float(m.group(3)),
        "ssao_bias": float(m.group(4)),
        "ssao_intensity": float(m.group(5)),
        "shadow_bias": float(m.group(6)),
        "shadow_pcf_radius": float(m.group(7)),
        "contact_patches": int(m.group(8)),
        "soft_penumbra": int(m.group(9)),
        "overlay_budget": int(m.group(10)),
        "dxr_required": m.group(11) == "1",
    }


def _cinematic_material_lighting_runtime(log_text: str) -> dict[str, int] | None:
    m = re.search(
        r"generative_exterior: cinematic material lighting "
        r"triplanar_layers=(\d+) relief_patches=(\d+) shadow_casters=(\d+) "
        r"contact_receivers=(\d+) localized_lights=(\d+) volumetric_slices=(\d+) wet_variation=(\d+)",
        log_text,
    )
    if not m:
        return None
    keys = (
        "triplanar_layers",
        "relief_patches",
        "shadow_casters",
        "contact_receivers",
        "localized_lights",
        "volumetric_slices",
        "wet_variation",
    )
    return {key: int(value) for key, value in zip(keys, m.groups())}


def _hero_asset_replacement_runtime(log_text: str) -> dict[str, int] | None:
    m = re.search(
        r"generative_exterior: hero asset replacement "
        r"canvas_shell=(\d+) fabric_layers=(\d+) structural_poles=(\d+) "
        r"rope_stakes=(\d+) low_poly_masks=(\d+) cabin_facade=(\d+) "
        r"cabin_roof=(\d+) cabin_deck_foundation=(\d+) hero_rock_masses=(\d+)",
        log_text,
    )
    if not m:
        return None
    keys = (
        "canvas_shell",
        "fabric_layers",
        "structural_poles",
        "rope_stakes",
        "low_poly_masks",
        "cabin_facade",
        "cabin_roof",
        "cabin_deck_foundation",
        "hero_rock_masses",
    )
    return {key: int(value) for key, value in zip(keys, m.groups())}


def _cohesive_staging_cleanup_runtime(log_text: str) -> dict[str, float | int] | None:
    m = re.search(
        r"generative_exterior: cohesive staging cleanup "
        r"cluster_radius=([0-9.]+) stray_budget=(\d+) sightline_clearance=([0-9.]+) "
        r"anchored_props=(\d+) foreground_relocated=(\d+) palette_unified=(\d+)",
        log_text,
    )
    if not m:
        return None
    return {
        "cluster_radius": float(m.group(1)),
        "stray_budget": int(m.group(2)),
        "sightline_clearance": float(m.group(3)),
        "anchored_props": int(m.group(4)),
        "foreground_relocated": int(m.group(5)),
        "palette_unified": int(m.group(6)),
    }


def _environment_fidelity_runtime(log_text: str) -> dict[str, float | int] | None:
    m = re.search(
        r"generative_exterior: environment fidelity "
        r"sky_layers=(\d+) atmosphere_cues=(\d+) horizon_bands=(\d+) "
        r"terrain_breakup=(\d+) water_depth_bands=(\d+) reflection_bands=(\d+) "
        r"shadow_lanes=(\d+) backdrop_blend=(\d+) shadow_directionality=([0-9.]+)",
        log_text,
    )
    if not m:
        return None
    keys = (
        "sky_layers",
        "atmosphere_cues",
        "horizon_bands",
        "terrain_breakup",
        "water_depth_bands",
        "reflection_bands",
        "shadow_lanes",
        "backdrop_blend",
    )
    values: dict[str, float | int] = {key: int(value) for key, value in zip(keys, m.groups()[:8])}
    values["shadow_directionality"] = float(m.group(9))
    return values


def _source_environment_runtime(log_text: str) -> dict[str, int] | None:
    m = re.search(
        r"generative_exterior: source environment assets "
        r"fetched_rocks=(\d+) kenney_cliffs=(\d+) detailed_trees=(\d+) "
        r"naturalistic_anchors=(\d+) terrain_replacements=(\d+) "
        r"backdrop_anchors=(\d+) source_sets=(\d+)",
        log_text,
    )
    if not m:
        return None
    keys = (
        "fetched_rocks",
        "kenney_cliffs",
        "detailed_trees",
        "naturalistic_anchors",
        "terrain_replacements",
        "backdrop_anchors",
        "source_sets",
    )
    return {key: int(value) for key, value in zip(keys, m.groups())}


def _hero_material_shadow_runtime(log_text: str) -> dict[str, float | int] | None:
    m = re.search(
        r"generative_exterior: hero material shadow readability "
        r"material_panels=(\d+) shadow_receivers=(\d+) "
        r"fill_lights=(\d+) rim_lights=(\d+) "
        r"material_contrast=([0-9.]+) exposure_lift=([0-9.]+)",
        log_text,
    )
    if not m:
        return None
    return {
        "material_panels": int(m.group(1)),
        "shadow_receivers": int(m.group(2)),
        "fill_lights": int(m.group(3)),
        "rim_lights": int(m.group(4)),
        "material_contrast": float(m.group(5)),
        "exposure_lift": float(m.group(6)),
    }


def _structural_scene_runtime(log_text: str) -> dict[str, float | int] | None:
    m = re.search(
        r"generative_exterior: structural scene fidelity "
        r"terrain_tiles=(\d+) displacement_layers=(\d+) "
        r"hero_foundations=(\d+) shadow_casters=(\d+) "
        r"material_blends=(\d+) light_volumes=(\d+) shore_segments=(\d+) "
        r"terrain_grid=(\d+) relief=([0-9.]+)",
        log_text,
    )
    if not m:
        return None
    return {
        "terrain_tiles": int(m.group(1)),
        "displacement_layers": int(m.group(2)),
        "hero_foundations": int(m.group(3)),
        "shadow_casters": int(m.group(4)),
        "material_blends": int(m.group(5)),
        "light_volumes": int(m.group(6)),
        "shore_segments": int(m.group(7)),
        "terrain_grid": int(m.group(8)),
        "relief": float(m.group(9)),
    }


def _hero_mesh_material_runtime(log_text: str) -> dict[str, int] | None:
    m = re.search(
        r"generative_exterior: hero mesh material overhaul "
        r"tent_shells=(\d+) cabin_cladding=(\d+) roof_layers=(\d+) "
        r"canyon_meshes=(\d+) pbr_layers=(\d+) silhouette_masks=(\d+)",
        log_text,
    )
    if not m:
        return None
    keys = ("tent_shells", "cabin_cladding", "roof_layers", "canyon_meshes", "pbr_layers", "silhouette_masks")
    return {key: int(value) for key, value in zip(keys, m.groups())}


def _lighting_shadow_material_field_runtime(log_text: str) -> dict[str, float | int] | None:
    m = re.search(
        r"generative_exterior: lighting shadow material field "
        r"shadowed_lights=(\d+) material_surfaces=(\d+) shadow_bands=(\d+) "
        r"probe_volumes=(\d+) key_fill=([0-9.]+) ambient=([0-9.]+) "
        r"ssao=([0-9.]+) shadow_bias=([0-9.]+) shadow_pcf=([0-9.]+)",
        log_text,
    )
    if not m:
        return None
    return {
        "shadowed_lights": int(m.group(1)),
        "material_surfaces": int(m.group(2)),
        "shadow_bands": int(m.group(3)),
        "probe_volumes": int(m.group(4)),
        "key_fill": float(m.group(5)),
        "ambient": float(m.group(6)),
        "ssao": float(m.group(7)),
        "shadow_bias": float(m.group(8)),
        "shadow_pcf": float(m.group(9)),
    }


def _source_readability_runtime(log_text: str) -> dict[str, float | int] | None:
    m = re.search(
        r"generative_exterior: source readability balance "
        r"source_surfaces=(\d+) backdrop_surfaces=(\d+) black_splits=(\d+) "
        r"albedo_floor=([0-9.]+) nonblack_floor=([0-9.]+)",
        log_text,
    )
    if not m:
        return None
    return {
        "source_surfaces": int(m.group(1)),
        "backdrop_surfaces": int(m.group(2)),
        "black_splits": int(m.group(3)),
        "albedo_floor": float(m.group(4)),
        "nonblack_floor": float(m.group(5)),
    }


def _asset_counts(ir: dict[str, Any]) -> dict[str, int]:
    counts = {
        "trees": 0,
        "pines": 0,
        "rocks": 0,
        "cliffs": 0,
        "hero": 0,
    }
    for obj in _objects(ir):
        low = str(obj.get("asset") or "").lower()
        if "tree" in low:
            counts["trees"] += 1
        if "pine" in low:
            counts["pines"] += 1
        if any(w in low for w in ("rock", "boulder", "stone", "cliff")):
            counts["rocks"] += 1
        if "cliff" in low:
            counts["cliffs"] += 1
        if any(w in low for w in ("tent", "campfire", "fire", "cabin", "log", "lantern")):
            counts["hero"] += 1
    return counts


def _image_metrics(path: Path | None) -> dict[str, Any]:
    if not path or not path.exists() or Image is None:
        return {}
    im = Image.open(path).convert("RGB")
    w, h = im.size
    full_samples = 0
    full_luma = 0.0
    full_nonblack = 0
    for y in range(0, h, 3):
        for x in range(0, w, 3):
            r, g, b = im.getpixel((x, y))
            l = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            full_luma += l
            if l >= 0.035:
                full_nonblack += 1
            full_samples += 1
    full_samples = max(full_samples, 1)
    # Lower-mid ground band: where planar terrain and ungrounded props dominate.
    box = (int(w * 0.06), int(h * 0.48), int(w * 0.94), int(h * 0.93))
    roi = im.crop(box)
    rw, rh = roi.size
    px = roi.load()
    samples = 0
    edge_sum = 0.0
    vertical_sum = 0.0
    dark_contact = 0
    dark_contact_area = 0
    for y in range(1, rh - 1, 2):
        for x in range(1, rw - 1, 2):
            r, g, b = px[x, y]
            l = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0
            rl, gl, bl = px[x - 1, y]
            rr, gr, br = px[x + 1, y]
            ru, gu, bu = px[x, y - 1]
            rd, gd, bd = px[x, y + 1]
            lx0 = (0.2126 * rl + 0.7152 * gl + 0.0722 * bl) / 255.0
            lx1 = (0.2126 * rr + 0.7152 * gr + 0.0722 * br) / 255.0
            ly0 = (0.2126 * ru + 0.7152 * gu + 0.0722 * bu) / 255.0
            ly1 = (0.2126 * rd + 0.7152 * gd + 0.0722 * bd) / 255.0
            gx = abs(lx1 - lx0)
            gy = abs(ly1 - ly0)
            edge_sum += math.sqrt(gx * gx + gy * gy)
            vertical_sum += gy
            if l < 0.10:
                dark_contact_area += 1
            if l < 0.075 and (gx + gy) > 0.055:
                dark_contact += 1
            samples += 1
    samples = max(samples, 1)
    return {
        "frame_avg_luma": round(full_luma / full_samples, 4),
        "frame_nonblack_fraction": round(full_nonblack / full_samples, 4),
        "frame_black_fraction": round(1.0 - (full_nonblack / full_samples), 4),
        "ground_box": box,
        "ground_edge_density": round(edge_sum / samples, 4),
        "ground_vertical_detail": round(vertical_sum / samples, 4),
        "dark_contact_fraction": round(dark_contact / samples, 4),
        "dark_contact_area_fraction": round(dark_contact_area / samples, 4),
        "sample_count": samples,
    }


def evaluate(
    prompt: str,
    ir: dict[str, Any],
    png: Path | None,
    log_text: str,
    frame_report: dict[str, Any] | None = None,
) -> dict[str, Any]:
    flags = _prompt_flags(prompt)
    graphics = _graphics(ir)
    ground = _ground(ir)
    terrain = ground.get("terrain") or {}
    materials = graphics.get("materials") or {}
    contact = graphics.get("contact") or {}
    renderer = graphics.get("renderer") or {}
    world_geometry = graphics.get("world_geometry") or {}
    shot = graphics.get("shot") or {}
    material_zones = graphics.get("material_zones") or {}
    asset_fidelity = graphics.get("asset_fidelity") or {}
    atmosphere_fidelity = graphics.get("atmosphere_fidelity") or {}
    geometry_realism = graphics.get("geometry_realism") or {}
    surface_material_richness = graphics.get("surface_material_richness") or {}
    mesh_silhouette_realism = graphics.get("mesh_silhouette_realism") or {}
    naturalistic_ecology = graphics.get("naturalistic_ecology") or {}
    lighting = graphics.get("lighting") or {}
    surface_detail = graphics.get("surface_detail") or {}
    occlusion = graphics.get("occlusion") or {}
    image_contact_occlusion = graphics.get("image_contact_occlusion") or {}
    water_shore_integration = graphics.get("water_shore_integration") or {}
    soft_occlusion = graphics.get("soft_occlusion") or {}
    hero_environment_geometry = graphics.get("hero_environment_geometry") or {}
    texture_material_fidelity = graphics.get("texture_material_fidelity") or {}
    source_geometry_fidelity = graphics.get("source_geometry_fidelity") or {}
    renderer_shadow_occlusion_budget = graphics.get("renderer_shadow_occlusion_budget") or {}
    authored_scene_module = graphics.get("authored_scene_module") or {}
    cinematic_material_lighting = graphics.get("cinematic_material_lighting") or {}
    hero_asset_replacement = graphics.get("hero_asset_replacement") or {}
    cohesive_staging_cleanup = graphics.get("cohesive_staging_cleanup") or {}
    environment_fidelity = graphics.get("environment_fidelity") or {}
    source_environment_assets = graphics.get("source_environment_assets") or {}
    hero_material_shadow_readability = graphics.get("hero_material_shadow_readability") or {}
    structural_scene_fidelity = graphics.get("structural_scene_fidelity") or {}
    hero_mesh_material_overhaul = graphics.get("hero_mesh_material_overhaul") or {}
    lighting_shadow_material_field = graphics.get("lighting_shadow_material_field") or {}
    source_readability_balance = graphics.get("source_readability_balance") or {}
    image = _image_metrics(png)
    frame_pipeline = _full_scene_pipeline_evidence(frame_report)

    failures: list[dict[str, Any]] = []
    warnings: list[dict[str, Any]] = []

    def fail(code: str, message: str, **detail: Any) -> None:
        failures.append({"code": code, "message": message, "detail": detail})

    def warn(code: str, message: str, **detail: Any) -> None:
        warnings.append({"code": code, "message": message, "detail": detail})

    if flags["exterior"] or ir.get("setting") == "exterior":
        relief = 0.0
        if isinstance(terrain, dict):
            try:
                relief = float(terrain.get("relief_m", 0.0) or 0.0)
            except Exception:
                relief = 0.0
        has_heightfield = isinstance(terrain, dict) and terrain.get("mode") == "heightfield" and relief >= 0.22
        has_runtime_heightfield = "generative_exterior: created procedural terrain heightfield" in log_text
        if not (has_heightfield and has_runtime_heightfield):
            fail(
                "missing_terrain_relief",
                "Generated exterior lacks non-flat terrain heightfield evidence",
                terrain=terrain,
                runtime_heightfield=has_runtime_heightfield,
            )

        decals = 0
        shore_layers = 0
        if isinstance(contact, dict):
            try:
                decals = int(contact.get("decal_count", 0) or 0)
                shore_layers = int(contact.get("shore_layer_count", 0) or 0)
            except Exception:
                decals = 0
        has_runtime_contact = "generative_exterior: created contact grounding" in log_text
        if decals < 6 or (flags["water"] and shore_layers < 2) or not has_runtime_contact:
            fail(
                "missing_contact_grounding",
                "Scene lacks explicit contact/shore grounding layers",
                contact=contact,
                runtime_contact=has_runtime_contact,
            )

        rich_objects = _material_detail_count(ir)
        has_material_contract = (
            isinstance(materials, dict)
            and bool(materials.get("enabled"))
            and rich_objects >= 8
            and float(materials.get("ground_normal_scale", 0.0) or 0.0) >= 0.55
            and float(materials.get("procedural_mask", 0.0) or 0.0) >= 0.20
        )
        has_runtime_materials = "generative_exterior: graphics material pass" in log_text
        if not (has_material_contract and has_runtime_materials):
            fail(
                "missing_material_pass",
                "Generated exterior lacks authored material detail controls",
                materials=materials,
                rich_object_materials=rich_objects,
                runtime_materials=has_runtime_materials,
            )

        advanced_terms = materials.get("advanced_shader_terms") if isinstance(materials, dict) else {}
        if isinstance(advanced_terms, dict):
            advanced_term_count = sum(1 for value in advanced_terms.values() if bool(value))
        else:
            advanced_term_count = 0
        advanced_objects = _advanced_material_count(ir)
        has_runtime_shader_materials = "generative_exterior: graphics shader material pass" in log_text
        if advanced_term_count < 4 or advanced_objects < 6 or not has_runtime_shader_materials:
            fail(
                "missing_advanced_shader_materials",
                "Scene lacks shader-backed material terms such as clearcoat/sheen/anisotropy/occlusion",
                advanced_terms=advanced_terms,
                advanced_term_count=advanced_term_count,
                advanced_object_materials=advanced_objects,
                runtime_shader_materials=has_runtime_shader_materials,
            )

        has_renderer_contract = (
            isinstance(renderer, dict)
            and bool(renderer.get("ssao"))
            and bool(renderer.get("ssr"))
            and bool(renderer.get("shadows"))
            and float(renderer.get("ssao_intensity", 0.0) or 0.0) >= 1.7
        )
        has_runtime_renderer = "generative_exterior: graphics renderer quality" in log_text
        if not (has_renderer_contract and has_runtime_renderer):
            fail(
                "missing_runtime_graphics_evidence",
                "No runtime evidence that AO/SSR/shadow graphics controls were applied",
                renderer=renderer,
                runtime_renderer=has_runtime_renderer,
            )

        if not frame_pipeline.get("ok"):
            fail(
                "missing_full_scene_shader_pipeline_evidence",
                "Generated exterior lacks per-render frame-contract evidence for the visibility-buffer material resolve/deferred lighting/full-scene shader path",
                frame_pipeline=frame_pipeline,
            )

        has_lighting_contract = (
            isinstance(lighting, dict)
            and bool(lighting.get("fixed_exposure"))
            and bool(lighting.get("raking_key"))
            and int(lighting.get("rim_light_count", 0) or 0) >= 1
        )
        has_runtime_lighting = "generative_exterior: graphics lighting pass" in log_text
        if not (has_lighting_contract and has_runtime_lighting):
            fail(
                "missing_lighting_shading_pass",
                "Generated exterior lacks manipulated lighting/shading evidence",
                lighting=lighting,
                runtime_lighting=has_runtime_lighting,
            )

        material_zone_count = _material_zone_count(material_zones)
        if material_zone_count < 4:
            fail(
                "insufficient_material_zone_variation",
                "Scene lacks enough distinct authored material zones for terrain/shore/rocks/water/vegetation",
                material_zones=material_zones,
                material_zone_count=material_zone_count,
            )

        try:
            ground_decals = int(surface_material_richness.get("ground_decal_count", 0) or 0)
            rock_patches = int(surface_material_richness.get("rock_lichen_patch_count", 0) or 0)
            desert_patches = int(surface_material_richness.get("desert_strata_patch_count", 0) or 0)
            vegetation_clusters = int(surface_material_richness.get("vegetation_cluster_count", 0) or 0)
            hero_lines = int(surface_material_richness.get("hero_material_line_count", 0) or 0)
        except Exception:
            ground_decals = rock_patches = desert_patches = vegetation_clusters = hero_lines = 0
        has_runtime_material_breakup = "generative_exterior: created material breakup decals" in log_text
        has_runtime_vegetation_clusters = "generative_exterior: created vegetation surface clusters" in log_text
        rock_or_desert_patches = rock_patches + desert_patches
        if (
            not isinstance(surface_material_richness, dict)
            or not bool(surface_material_richness.get("enabled"))
            or ground_decals < 12
            or rock_or_desert_patches < 8
            or vegetation_clusters < 8
            or (flags["campsite"] and hero_lines < 12)
            or ("cabin" in prompt.lower() and hero_lines < 12)
            or not (has_runtime_material_breakup and has_runtime_vegetation_clusters)
        ):
            fail(
                "missing_surface_material_richness",
                "Generated exterior lacks visible material breakup decals, close-prop material lines, or vegetation/scrub surface clusters",
                surface_material_richness=surface_material_richness,
                ground_decal_count=ground_decals,
                rock_or_desert_patch_count=rock_or_desert_patches,
                vegetation_cluster_count=vegetation_clusters,
                hero_material_line_count=hero_lines,
                runtime_material_breakup=has_runtime_material_breakup,
                runtime_vegetation_clusters=has_runtime_vegetation_clusters,
            )

        try:
            cliff_bands = int(mesh_silhouette_realism.get("cliff_mesh_vertical_bands", 0) or 0)
            cliff_overhangs = int(mesh_silhouette_realism.get("cliff_overhang_count", 0) or 0)
            hero_bevels = int(mesh_silhouette_realism.get("hero_bevel_detail_count", 0) or 0)
            prop_depth_layers = int(mesh_silhouette_realism.get("prop_depth_layer_count", 0) or 0)
        except Exception:
            cliff_bands = cliff_overhangs = hero_bevels = prop_depth_layers = 0
        has_runtime_faceted_cliff = "generative_exterior: created faceted cliff mesh" in log_text
        has_runtime_hero_silhouette = "generative_exterior: created hero silhouette bevel detail" in log_text
        hero_prompt = flags["campsite"] or "cabin" in prompt.lower()
        canyon_prompt = flags["canyon"] or "canyon" in str((ir.get("director") or {}).get("scene_type", "")).lower()
        if (
            not isinstance(mesh_silhouette_realism, dict)
            or not bool(mesh_silhouette_realism.get("enabled"))
            or (hero_prompt and (hero_bevels < 10 or prop_depth_layers < 6 or not has_runtime_hero_silhouette))
            or (canyon_prompt and (cliff_bands < 4 or cliff_overhangs < 8 or not has_runtime_faceted_cliff))
        ):
            fail(
                "missing_mesh_silhouette_realism",
                "Generated exterior lacks faceted cliff mesh bands or hero bevel/eave/hem silhouette detail",
                mesh_silhouette_realism=mesh_silhouette_realism,
                cliff_mesh_vertical_bands=cliff_bands,
                cliff_overhang_count=cliff_overhangs,
                hero_bevel_detail_count=hero_bevels,
                prop_depth_layer_count=prop_depth_layers,
                runtime_faceted_cliff=has_runtime_faceted_cliff,
                runtime_hero_silhouette=has_runtime_hero_silhouette,
            )

        try:
            natural_grass = int(naturalistic_ecology.get("grass_cluster_count", 0) or 0)
            natural_bush = int(naturalistic_ecology.get("bush_cluster_count", 0) or 0)
            natural_fern = int(naturalistic_ecology.get("fern_cluster_count", 0) or 0)
            natural_trunks = int(naturalistic_ecology.get("trunk_count", 0) or 0)
            natural_branches = int(naturalistic_ecology.get("branch_count", 0) or 0)
            natural_stumps = int(naturalistic_ecology.get("stump_count", 0) or 0)
            natural_moss_rocks = int(naturalistic_ecology.get("moss_rock_count", 0) or 0)
        except Exception:
            natural_grass = natural_bush = natural_fern = natural_trunks = natural_branches = natural_stumps = natural_moss_rocks = 0
        natural_leafy = natural_grass + natural_bush + natural_fern
        natural_woody = natural_trunks + natural_branches + natural_stumps
        natural_total = natural_leafy + natural_woody + natural_moss_rocks
        desert_like = flags["desert"] or flags["canyon"]
        has_runtime_naturalistic = "generative_exterior: created naturalistic ecology assets" in log_text
        if (
            not isinstance(naturalistic_ecology, dict)
            or not bool(naturalistic_ecology.get("enabled"))
            or not has_runtime_naturalistic
            or natural_total < (10 if desert_like else 20)
            or (not desert_like and (natural_grass < 8 or (natural_bush + natural_fern) < 4 or natural_woody < 3))
            or (desert_like and ((natural_branches + natural_trunks) < 5 or natural_moss_rocks < 3))
        ):
            fail(
                "missing_naturalistic_ecology_assets",
                "Generated exterior lacks scanned naturalistic grass/brush/branches/stumps/rocks to break up game-kit vegetation",
                naturalistic_ecology=naturalistic_ecology,
                grass_cluster_count=natural_grass,
                bush_cluster_count=natural_bush,
                fern_cluster_count=natural_fern,
                trunk_count=natural_trunks,
                branch_count=natural_branches,
                stump_count=natural_stumps,
                moss_rock_count=natural_moss_rocks,
                total_naturalistic_instances=natural_total,
                runtime_naturalistic_ecology=has_runtime_naturalistic,
            )

        try:
            hero_detail_count = int(asset_fidelity.get("hero_detail_count", 0) or 0)
            camp_detail_count = int(asset_fidelity.get("camp_detail_count", 0) or 0)
            cabin_facade_count = int(asset_fidelity.get("cabin_facade_detail_count", 0) or 0)
            backdrop_detail_layers = int(asset_fidelity.get("backdrop_detail_layers", 0) or 0)
        except Exception:
            hero_detail_count = camp_detail_count = cabin_facade_count = backdrop_detail_layers = 0
        prompt_has_cabin = "cabin" in prompt.lower()
        has_runtime_asset_fidelity = "generative_exterior: created hero asset detail" in log_text
        has_runtime_backdrop_detail = "generative_exterior: created backdrop silhouette detail" in log_text
        if (
            not isinstance(asset_fidelity, dict)
            or not bool(asset_fidelity.get("enabled"))
            or hero_detail_count < 12
            or backdrop_detail_layers < 3
            or (flags["campsite"] and camp_detail_count < 10)
            or (prompt_has_cabin and cabin_facade_count < 14)
            or not (has_runtime_asset_fidelity and has_runtime_backdrop_detail)
        ):
            fail(
                "missing_asset_fidelity_detail",
                "Generated exterior lacks close-range hero asset construction detail and richer backdrop silhouettes",
                asset_fidelity=asset_fidelity,
                runtime_asset_fidelity=has_runtime_asset_fidelity,
                runtime_backdrop_detail=has_runtime_backdrop_detail,
            )

        try:
            high_detail_camp_pieces = int(hero_environment_geometry.get("high_detail_camp_piece_count", 0) or 0)
            high_detail_cabin_pieces = int(hero_environment_geometry.get("high_detail_cabin_piece_count", 0) or 0)
            mountain_mass_layers = int(hero_environment_geometry.get("mountain_mass_layer_count", 0) or 0)
            cliff_mass_pieces = int(hero_environment_geometry.get("cliff_mass_piece_count", 0) or 0)
            shoreline_props = int(hero_environment_geometry.get("shoreline_prop_count", 0) or 0)
            irregular_tree_silhouettes = int(hero_environment_geometry.get("irregular_tree_silhouette_count", 0) or 0)
        except Exception:
            high_detail_camp_pieces = high_detail_cabin_pieces = mountain_mass_layers = 0
            cliff_mass_pieces = shoreline_props = irregular_tree_silhouettes = 0
        has_runtime_hero_env = "generative_exterior: created hero environment geometry" in log_text
        has_runtime_camp_kit = "generative_exterior: created high detail camp kit" in log_text
        has_runtime_cabin_kit = "generative_exterior: created high detail cabin kit" in log_text
        has_runtime_mountain_mass = "generative_exterior: created mountain massing geometry" in log_text
        has_runtime_tree_silhouette = "generative_exterior: created irregular tree silhouette geometry" in log_text
        prompt_has_cabin = "cabin" in prompt.lower()
        needs_non_desert_trees = not (flags["desert"] or flags["canyon"])
        if (
            not isinstance(hero_environment_geometry, dict)
            or not bool(hero_environment_geometry.get("enabled"))
            or not has_runtime_hero_env
            or mountain_mass_layers < 3
            or not has_runtime_mountain_mass
            or (flags["water"] and shoreline_props < 6)
            or (flags["campsite"] and (high_detail_camp_pieces < 24 or not has_runtime_camp_kit))
            or (prompt_has_cabin and (high_detail_cabin_pieces < 22 or not has_runtime_cabin_kit))
            or (canyon_prompt and cliff_mass_pieces < 8)
            or (needs_non_desert_trees and (irregular_tree_silhouettes < 8 or not has_runtime_tree_silhouette))
        ):
            fail(
                "missing_hero_environment_geometry",
                "Generated exterior lacks high-detail hero/environment geometry for camp/cabin construction, mountain/cliff massing, shoreline props, or tree silhouettes",
                hero_environment_geometry=hero_environment_geometry,
                high_detail_camp_piece_count=high_detail_camp_pieces,
                high_detail_cabin_piece_count=high_detail_cabin_pieces,
                mountain_mass_layer_count=mountain_mass_layers,
                cliff_mass_piece_count=cliff_mass_pieces,
                shoreline_prop_count=shoreline_props,
                irregular_tree_silhouette_count=irregular_tree_silhouettes,
                runtime_hero_environment_geometry=has_runtime_hero_env,
                runtime_high_detail_camp_kit=has_runtime_camp_kit,
                runtime_high_detail_cabin_kit=has_runtime_cabin_kit,
                runtime_mountain_massing_geometry=has_runtime_mountain_mass,
                runtime_irregular_tree_silhouette_geometry=has_runtime_tree_silhouette,
            )

        try:
            texture_set_count = int(texture_material_fidelity.get("texture_set_count", 0) or 0)
            terrain_surfaces = int(texture_material_fidelity.get("terrain_surface_count", 0) or 0)
            rock_surfaces = int(texture_material_fidelity.get("rock_surface_count", 0) or 0)
            wood_surfaces = int(texture_material_fidelity.get("wood_surface_count", 0) or 0)
            fabric_surfaces = int(texture_material_fidelity.get("fabric_surface_count", 0) or 0)
            hero_surfaces = int(texture_material_fidelity.get("hero_surface_count", 0) or 0)
            shore_surfaces = int(texture_material_fidelity.get("shore_surface_count", 0) or 0)
        except Exception:
            texture_set_count = terrain_surfaces = rock_surfaces = wood_surfaces = 0
            fabric_surfaces = hero_surfaces = shore_surfaces = 0
        needs_fabric = flags["campsite"]
        needs_wood = flags["campsite"] or "cabin" in prompt.lower() or flags["water"]
        min_terrain_surfaces = 2 if flags["water"] else 1
        runtime_texture_counts = _texture_runtime_counts(log_text)
        runtime_texture_ok = (
            isinstance(runtime_texture_counts, dict)
            and runtime_texture_counts.get("texture_sets", 0) >= 4
            and runtime_texture_counts.get("terrain", 0) >= min_terrain_surfaces
            and runtime_texture_counts.get("rock", 0) >= 6
            and runtime_texture_counts.get("hero", 0) >= 10
            and (not needs_wood or runtime_texture_counts.get("wood", 0) >= 8)
            and (not needs_fabric or runtime_texture_counts.get("fabric", 0) >= 4)
            and (not flags["water"] or runtime_texture_counts.get("shore", 0) >= 4)
        )
        if (
            not isinstance(texture_material_fidelity, dict)
            or not bool(texture_material_fidelity.get("enabled"))
            or texture_set_count < 4
            or terrain_surfaces < min_terrain_surfaces
            or rock_surfaces < 6
            or hero_surfaces < 10
            or (needs_wood and wood_surfaces < 8)
            or (needs_fabric and fabric_surfaces < 4)
            or (flags["water"] and shore_surfaces < 4)
            or not runtime_texture_ok
        ):
            fail(
                "missing_texture_material_fidelity",
                "Generated exterior lacks texture-backed material binding for terrain, rock/cliff, wood, fabric, or hero surfaces",
                texture_material_fidelity=texture_material_fidelity,
                texture_set_count=texture_set_count,
                terrain_surface_count=terrain_surfaces,
                rock_surface_count=rock_surfaces,
                wood_surface_count=wood_surfaces,
                fabric_surface_count=fabric_surfaces,
                hero_surface_count=hero_surfaces,
                shore_surface_count=shore_surfaces,
                runtime_texture_counts=runtime_texture_counts,
                runtime_texture_materials=runtime_texture_ok,
            )

        try:
            source_sets = int(source_geometry_fidelity.get("source_asset_set_count", 0) or 0)
            scanned_lanterns = int(source_geometry_fidelity.get("scanned_lantern_count", 0) or 0)
            scanned_utility_props = int(source_geometry_fidelity.get("scanned_utility_prop_count", 0) or 0)
            scanned_anchor_rocks = int(source_geometry_fidelity.get("scanned_anchor_rock_count", 0) or 0)
            source_hero_anchors = int(source_geometry_fidelity.get("hero_anchor_count", 0) or 0)
        except Exception:
            source_sets = scanned_lanterns = scanned_utility_props = scanned_anchor_rocks = source_hero_anchors = 0
        has_runtime_source_geometry = "generative_exterior: source-bound hero geometry" in log_text
        hero_prompt = flags["campsite"] or "cabin" in prompt.lower()
        if (
            not isinstance(source_geometry_fidelity, dict)
            or not bool(source_geometry_fidelity.get("enabled"))
            or source_sets < 3
            or scanned_anchor_rocks < 3
            or source_hero_anchors < 4
            or (hero_prompt and scanned_lanterns < 1)
            or (hero_prompt and scanned_utility_props < 2)
            or not has_runtime_source_geometry
        ):
            fail(
                "missing_source_bound_hero_geometry",
                "Generated exterior lacks source-bound scanned hero meshes for prompt anchors, props, and grounding rocks",
                source_geometry_fidelity=source_geometry_fidelity,
                source_asset_set_count=source_sets,
                scanned_lantern_count=scanned_lanterns,
                scanned_utility_prop_count=scanned_utility_props,
                scanned_anchor_rock_count=scanned_anchor_rocks,
                hero_anchor_count=source_hero_anchors,
                runtime_source_geometry=has_runtime_source_geometry,
            )

        try:
            budget_ssao_radius = float(renderer_shadow_occlusion_budget.get("ssao_radius", 0.0) or 0.0)
            budget_ssao_bias = float(renderer_shadow_occlusion_budget.get("ssao_bias", 1.0) or 1.0)
            budget_ssao_intensity = float(renderer_shadow_occlusion_budget.get("ssao_intensity", 0.0) or 0.0)
            budget_shadow_bias = float(renderer_shadow_occlusion_budget.get("shadow_bias", 1.0) or 1.0)
            budget_shadow_pcf = float(renderer_shadow_occlusion_budget.get("shadow_pcf_radius", 0.0) or 0.0)
            contact_patch_budget = int(renderer_shadow_occlusion_budget.get("contact_receiver_patch_budget", 0) or 0)
            soft_penumbra_budget = int(renderer_shadow_occlusion_budget.get("soft_penumbra_patch_budget", 0) or 0)
            renderer_contact_blend = float(renderer_shadow_occlusion_budget.get("renderer_contact_blend", 0.0) or 0.0)
        except Exception:
            budget_ssao_radius = budget_ssao_intensity = budget_shadow_pcf = renderer_contact_blend = 0.0
            budget_ssao_bias = budget_shadow_bias = 1.0
            contact_patch_budget = soft_penumbra_budget = 0
        runtime_shadow_budget = _renderer_shadow_runtime(log_text)
        runtime_shadow_budget_ok = (
            isinstance(runtime_shadow_budget, dict)
            and runtime_shadow_budget.get("ssao") is True
            and runtime_shadow_budget.get("shadows") is True
            and runtime_shadow_budget.get("dxr_required") is False
            and runtime_shadow_budget.get("ssao_radius", 0.0) >= 1.05
            and runtime_shadow_budget.get("ssao_bias", 1.0) <= 0.025
            and runtime_shadow_budget.get("ssao_intensity", 0.0) >= 2.10
            and runtime_shadow_budget.get("shadow_bias", 1.0) <= 0.0030
            and runtime_shadow_budget.get("shadow_pcf_radius", 0.0) >= 2.40
            and runtime_shadow_budget.get("contact_patches", 0) >= 12
            and runtime_shadow_budget.get("contact_patches", 9999) <= max(contact_patch_budget, 1)
            and runtime_shadow_budget.get("soft_penumbra", 9999) <= max(soft_penumbra_budget, 1)
            and runtime_shadow_budget.get("overlay_budget", 9999) <= max(contact_patch_budget + soft_penumbra_budget, 1)
            and abs(runtime_shadow_budget.get("ssao_radius", 0.0) - budget_ssao_radius) <= 0.18
            and abs(runtime_shadow_budget.get("ssao_intensity", 0.0) - budget_ssao_intensity) <= 0.20
            and abs(runtime_shadow_budget.get("shadow_pcf_radius", 0.0) - budget_shadow_pcf) <= 0.20
        )
        if (
            not isinstance(renderer_shadow_occlusion_budget, dict)
            or not bool(renderer_shadow_occlusion_budget.get("enabled"))
            or not bool(renderer_shadow_occlusion_budget.get("renderer_ssao"))
            or not bool(renderer_shadow_occlusion_budget.get("shadow_maps"))
            or bool(renderer_shadow_occlusion_budget.get("dxr_required"))
            or budget_ssao_radius < 1.05
            or budget_ssao_bias > 0.025
            or budget_ssao_intensity < 2.10
            or budget_shadow_bias > 0.0030
            or budget_shadow_pcf < 2.40
            or contact_patch_budget < 24
            or soft_penumbra_budget < 18
            or renderer_contact_blend < 0.60
            or not runtime_shadow_budget_ok
        ):
            fail(
                "missing_renderer_shadow_occlusion_budget",
                "Generated exterior lacks bounded renderer-level SSAO/shadow-map contact budget evidence",
                renderer_shadow_occlusion_budget=renderer_shadow_occlusion_budget,
                runtime_shadow_occlusion_budget=runtime_shadow_budget,
                runtime_shadow_occlusion_budget_ok=runtime_shadow_budget_ok,
            )

        try:
            module_id = str(authored_scene_module.get("module_id", "") or "")
            composition_anchors = int(authored_scene_module.get("composition_anchor_count", 0) or 0)
            terrain_setpieces = int(authored_scene_module.get("terrain_setpiece_count", 0) or 0)
            hero_clusters = int(authored_scene_module.get("hero_cluster_count", 0) or 0)
            foreground_frames = int(authored_scene_module.get("foreground_frame_count", 0) or 0)
            backdrop_gates = int(authored_scene_module.get("backdrop_gate_count", 0) or 0)
            lighting_zones = int(authored_scene_module.get("lighting_zone_count", 0) or 0)
            material_families = int(authored_scene_module.get("material_family_count", 0) or 0)
            water_shape_segments = int(authored_scene_module.get("water_shape_segment_count", 0) or 0)
            practical_lights = int(authored_scene_module.get("practical_light_count", 0) or 0)
        except Exception:
            module_id = ""
            composition_anchors = terrain_setpieces = hero_clusters = foreground_frames = 0
            backdrop_gates = lighting_zones = material_families = water_shape_segments = practical_lights = 0
        expected_modules = {
            "campsite_lake_dawn",
            "desert_canyon_river",
            "alpine_cabin_lake",
            "exterior_water_setpiece",
            "exterior_landscape_setpiece",
        }
        has_runtime_authored_module = re.search(
            r"generative_exterior: authored scene module module=([a-z0-9_]+) "
            r"anchors=(\d+) terrain_setpieces=(\d+) hero_clusters=(\d+) "
            r"foreground_frames=(\d+) backdrop_gates=(\d+) lighting_zones=(\d+) "
            r"material_families=(\d+) water_segments=(\d+) practical_lights=(\d+)",
            log_text,
        )
        runtime_module_ok = False
        if has_runtime_authored_module:
            runtime_module = has_runtime_authored_module.group(1)
            runtime_counts = [int(v) for v in has_runtime_authored_module.groups()[1:]]
            runtime_module_ok = (
                runtime_module == module_id
                and runtime_counts[0] >= 6
                and runtime_counts[1] >= 4
                and runtime_counts[2] >= 2
                and runtime_counts[3] >= 3
                and runtime_counts[4] >= 3
                and runtime_counts[5] >= 2
                and runtime_counts[6] >= 5
                and (not flags["water"] or runtime_counts[7] >= 6)
                and runtime_counts[8] >= (2 if (flags["campsite"] or "cabin" in prompt.lower()) else 1)
            )
        if (
            not isinstance(authored_scene_module, dict)
            or not bool(authored_scene_module.get("enabled"))
            or module_id not in expected_modules
            or composition_anchors < 6
            or terrain_setpieces < 4
            or hero_clusters < 2
            or foreground_frames < 3
            or backdrop_gates < 3
            or lighting_zones < 2
            or material_families < 5
            or (flags["water"] and water_shape_segments < 6)
            or ((flags["campsite"] or "cabin" in prompt.lower()) and practical_lights < 2)
            or not runtime_module_ok
        ):
            fail(
                "missing_authored_scene_module",
                "Generated exterior lacks a cohesive source-authored scene module with foreground/midground/backdrop composition and authored lighting",
                authored_scene_module=authored_scene_module,
                module_id=module_id,
                composition_anchor_count=composition_anchors,
                terrain_setpiece_count=terrain_setpieces,
                hero_cluster_count=hero_clusters,
                foreground_frame_count=foreground_frames,
                backdrop_gate_count=backdrop_gates,
                lighting_zone_count=lighting_zones,
                material_family_count=material_families,
                water_shape_segment_count=water_shape_segments,
                practical_light_count=practical_lights,
                runtime_authored_scene_module=has_runtime_authored_module.group(0) if has_runtime_authored_module else None,
                runtime_authored_scene_module_ok=runtime_module_ok,
            )

        try:
            triplanar_layers = int(cinematic_material_lighting.get("triplanar_detail_layer_count", 0) or 0)
            relief_patches = int(cinematic_material_lighting.get("terrain_relief_patch_count", 0) or 0)
            shadow_casters = int(cinematic_material_lighting.get("shadow_caster_count", 0) or 0)
            contact_receivers = int(cinematic_material_lighting.get("contact_receiver_count", 0) or 0)
            localized_lights = int(cinematic_material_lighting.get("localized_light_count", 0) or 0)
            volumetric_slices = int(cinematic_material_lighting.get("volumetric_light_slice_count", 0) or 0)
            wet_variation = int(cinematic_material_lighting.get("wet_roughness_variation_count", 0) or 0)
            source_texture_weight = float(cinematic_material_lighting.get("source_texture_weight", 0.0) or 0.0)
            normal_detail_scale = float(cinematic_material_lighting.get("normal_detail_scale", 0.0) or 0.0)
        except Exception:
            triplanar_layers = relief_patches = shadow_casters = contact_receivers = 0
            localized_lights = volumetric_slices = wet_variation = 0
            source_texture_weight = normal_detail_scale = 0.0
        runtime_cinematic = _cinematic_material_lighting_runtime(log_text)
        runtime_cinematic_ok = (
            isinstance(runtime_cinematic, dict)
            and runtime_cinematic.get("triplanar_layers", 0) >= 6
            and runtime_cinematic.get("relief_patches", 0) >= 16
            and runtime_cinematic.get("shadow_casters", 0) >= 6
            and runtime_cinematic.get("contact_receivers", 0) >= 12
            and runtime_cinematic.get("localized_lights", 0) >= 2
            and runtime_cinematic.get("volumetric_slices", 0) >= 3
            and (not flags["water"] or runtime_cinematic.get("wet_variation", 0) >= 6)
        )
        if (
            not isinstance(cinematic_material_lighting, dict)
            or not bool(cinematic_material_lighting.get("enabled"))
            or triplanar_layers < 6
            or relief_patches < 16
            or shadow_casters < 6
            or contact_receivers < 12
            or localized_lights < 2
            or volumetric_slices < 3
            or source_texture_weight < 0.65
            or normal_detail_scale < 0.75
            or (flags["water"] and wet_variation < 6)
            or not runtime_cinematic_ok
        ):
            fail(
                "missing_cinematic_material_lighting_pass",
                "Generated exterior lacks integrated triplanar material relief, localized shadow casters/receivers, and cinematic light/fog slices",
                cinematic_material_lighting=cinematic_material_lighting,
                triplanar_detail_layer_count=triplanar_layers,
                terrain_relief_patch_count=relief_patches,
                shadow_caster_count=shadow_casters,
                contact_receiver_count=contact_receivers,
                localized_light_count=localized_lights,
                volumetric_light_slice_count=volumetric_slices,
                wet_roughness_variation_count=wet_variation,
                source_texture_weight=source_texture_weight,
                normal_detail_scale=normal_detail_scale,
                runtime_cinematic_material_lighting=runtime_cinematic,
                runtime_cinematic_material_lighting_ok=runtime_cinematic_ok,
            )

        try:
            canvas_shell = int(hero_asset_replacement.get("canvas_shell_panel_count", 0) or 0)
            fabric_layers = int(hero_asset_replacement.get("fabric_layer_count", 0) or 0)
            structural_poles = int(hero_asset_replacement.get("structural_pole_count", 0) or 0)
            rope_stakes = int(hero_asset_replacement.get("rope_stake_count", 0) or 0)
            low_poly_masks = int(hero_asset_replacement.get("low_poly_mask_count", 0) or 0)
            cabin_facade = int(hero_asset_replacement.get("cabin_facade_module_count", 0) or 0)
            cabin_roof = int(hero_asset_replacement.get("cabin_roof_module_count", 0) or 0)
            cabin_deck_foundation = int(hero_asset_replacement.get("cabin_deck_foundation_count", 0) or 0)
            hero_rock_masses = int(hero_asset_replacement.get("hero_rock_mass_count", 0) or 0)
        except Exception:
            canvas_shell = fabric_layers = structural_poles = rope_stakes = low_poly_masks = 0
            cabin_facade = cabin_roof = cabin_deck_foundation = hero_rock_masses = 0
        runtime_replacement = _hero_asset_replacement_runtime(log_text)
        runtime_replacement_ok = isinstance(runtime_replacement, dict)
        if runtime_replacement_ok:
            if flags["campsite"]:
                runtime_replacement_ok = (
                    runtime_replacement.get("canvas_shell", 0) >= 10
                    and runtime_replacement.get("fabric_layers", 0) >= 10
                    and runtime_replacement.get("structural_poles", 0) >= 6
                    and runtime_replacement.get("rope_stakes", 0) >= 8
                    and runtime_replacement.get("low_poly_masks", 0) >= 2
                )
            if "cabin" in prompt.lower():
                runtime_replacement_ok = runtime_replacement_ok and (
                    runtime_replacement.get("cabin_facade", 0) >= 12
                    and runtime_replacement.get("cabin_roof", 0) >= 6
                    and runtime_replacement.get("cabin_deck_foundation", 0) >= 6
                )
            if canyon_prompt:
                runtime_replacement_ok = runtime_replacement_ok and runtime_replacement.get("hero_rock_masses", 0) >= 8
            if not (flags["campsite"] or "cabin" in prompt.lower() or canyon_prompt):
                runtime_replacement_ok = sum(runtime_replacement.values()) >= 8
        if (
            not isinstance(hero_asset_replacement, dict)
            or not bool(hero_asset_replacement.get("enabled"))
            or (flags["campsite"] and (canvas_shell < 10 or fabric_layers < 10 or structural_poles < 6 or rope_stakes < 8 or low_poly_masks < 2))
            or ("cabin" in prompt.lower() and (cabin_facade < 12 or cabin_roof < 6 or cabin_deck_foundation < 6))
            or (canyon_prompt and hero_rock_masses < 8)
            or (not (flags["campsite"] or "cabin" in prompt.lower() or canyon_prompt) and (canvas_shell + fabric_layers + cabin_facade + hero_rock_masses) < 8)
            or not runtime_replacement_ok
        ):
            fail(
                "missing_hero_asset_replacement",
                "Generated exterior still lacks dominant hero asset replacement/overbuild for low-poly tent, cabin, or canyon forms",
                hero_asset_replacement=hero_asset_replacement,
                canvas_shell_panel_count=canvas_shell,
                fabric_layer_count=fabric_layers,
                structural_pole_count=structural_poles,
                rope_stake_count=rope_stakes,
                low_poly_mask_count=low_poly_masks,
                cabin_facade_module_count=cabin_facade,
                cabin_roof_module_count=cabin_roof,
                cabin_deck_foundation_count=cabin_deck_foundation,
                hero_rock_mass_count=hero_rock_masses,
                runtime_hero_asset_replacement=runtime_replacement,
                runtime_hero_asset_replacement_ok=runtime_replacement_ok,
            )

        try:
            cluster_radius = float(cohesive_staging_cleanup.get("hero_cluster_radius_m", 0.0) or 0.0)
            stray_budget = int(cohesive_staging_cleanup.get("stray_dressing_budget", 99) or 99)
            sightline_clearance = float(cohesive_staging_cleanup.get("central_sightline_clearance_m", 0.0) or 0.0)
            anchored_props = int(cohesive_staging_cleanup.get("anchored_prop_count", 0) or 0)
            foreground_relocated = int(cohesive_staging_cleanup.get("foreground_relocation_count", 0) or 0)
            palette_unified = int(cohesive_staging_cleanup.get("palette_unification_count", 0) or 0)
        except Exception:
            cluster_radius = sightline_clearance = 0.0
            stray_budget = 99
            anchored_props = foreground_relocated = palette_unified = 0
        runtime_staging = _cohesive_staging_cleanup_runtime(log_text)
        runtime_staging_ok = (
            isinstance(runtime_staging, dict)
            and runtime_staging.get("cluster_radius", 99.0) <= max(cluster_radius + 0.25, 0.25)
            and runtime_staging.get("stray_budget", 99) <= max(stray_budget, 0)
            and runtime_staging.get("sightline_clearance", 0.0) >= max(sightline_clearance - 0.25, 0.0)
            and runtime_staging.get("anchored_props", 0) >= max(anchored_props - 2, 0)
            and runtime_staging.get("foreground_relocated", 0) >= max(foreground_relocated - 1, 0)
            and runtime_staging.get("palette_unified", 0) >= max(palette_unified - 2, 0)
        )
        if (
            not isinstance(cohesive_staging_cleanup, dict)
            or not bool(cohesive_staging_cleanup.get("enabled"))
            or cluster_radius <= 0.0
            or cluster_radius > 3.25
            or stray_budget > 3
            or sightline_clearance < 3.0
            or anchored_props < (12 if (flags["campsite"] or "cabin" in prompt.lower()) else 6)
            or foreground_relocated < 3
            or palette_unified < 8
            or not runtime_staging_ok
        ):
            fail(
                "missing_cohesive_staging_cleanup",
                "Generated exterior still has disconnected prop scatter instead of clustered hero staging and a clear central sightline",
                cohesive_staging_cleanup=cohesive_staging_cleanup,
                hero_cluster_radius_m=cluster_radius,
                stray_dressing_budget=stray_budget,
                central_sightline_clearance_m=sightline_clearance,
                anchored_prop_count=anchored_props,
                foreground_relocation_count=foreground_relocated,
                palette_unification_count=palette_unified,
                runtime_cohesive_staging_cleanup=runtime_staging,
                runtime_cohesive_staging_cleanup_ok=runtime_staging_ok,
            )

        try:
            sky_layers = int(environment_fidelity.get("sky_layer_count", 0) or 0)
            atmosphere_cues = int(environment_fidelity.get("atmosphere_depth_cue_count", 0) or 0)
            horizon_bands = int(environment_fidelity.get("horizon_blend_band_count", 0) or 0)
            terrain_breakup = int(environment_fidelity.get("terrain_macro_breakup_count", 0) or 0)
            water_depth_bands = int(environment_fidelity.get("water_depth_band_count", 0) or 0)
            reflection_bands = int(environment_fidelity.get("reflection_band_count", 0) or 0)
            shadow_lanes = int(environment_fidelity.get("directional_shadow_lane_count", 0) or 0)
            backdrop_blend = int(environment_fidelity.get("backdrop_integration_layer_count", 0) or 0)
            shadow_directionality = float(environment_fidelity.get("shadow_directionality", 0.0) or 0.0)
        except Exception:
            sky_layers = atmosphere_cues = horizon_bands = terrain_breakup = 0
            water_depth_bands = reflection_bands = shadow_lanes = backdrop_blend = 0
            shadow_directionality = 0.0
        runtime_environment = _environment_fidelity_runtime(log_text)
        runtime_environment_ok = (
            isinstance(runtime_environment, dict)
            and runtime_environment.get("sky_layers", 0) >= max(sky_layers - 1, 3)
            and runtime_environment.get("atmosphere_cues", 0) >= max(atmosphere_cues - 1, 4)
            and runtime_environment.get("horizon_bands", 0) >= max(horizon_bands - 1, 3)
            and runtime_environment.get("terrain_breakup", 0) >= max(terrain_breakup - 2, 10)
            and runtime_environment.get("shadow_lanes", 0) >= max(shadow_lanes - 1, 6)
            and runtime_environment.get("backdrop_blend", 0) >= max(backdrop_blend - 1, 5)
            and runtime_environment.get("shadow_directionality", 0.0) >= max(shadow_directionality - 0.05, 0.60)
            and (
                not flags["water"]
                or (
                    runtime_environment.get("water_depth_bands", 0) >= max(water_depth_bands - 1, 4)
                    and runtime_environment.get("reflection_bands", 0) >= max(reflection_bands - 1, 4)
                )
            )
        )
        if (
            not isinstance(environment_fidelity, dict)
            or not bool(environment_fidelity.get("enabled"))
            or sky_layers < 4
            or atmosphere_cues < 5
            or horizon_bands < 3
            or terrain_breakup < 12
            or shadow_lanes < 6
            or backdrop_blend < 5
            or shadow_directionality < 0.65
            or (flags["water"] and (water_depth_bands < 5 or reflection_bands < 4))
            or not runtime_environment_ok
        ):
            fail(
                "missing_environment_fidelity_pass",
                "Generated exterior lacks a single integrated sky/atmosphere/water/terrain/backdrop/shadow fidelity pass",
                environment_fidelity=environment_fidelity,
                sky_layer_count=sky_layers,
                atmosphere_depth_cue_count=atmosphere_cues,
                horizon_blend_band_count=horizon_bands,
                terrain_macro_breakup_count=terrain_breakup,
                water_depth_band_count=water_depth_bands,
                reflection_band_count=reflection_bands,
                directional_shadow_lane_count=shadow_lanes,
                backdrop_integration_layer_count=backdrop_blend,
                shadow_directionality=shadow_directionality,
                runtime_environment_fidelity=runtime_environment,
                runtime_environment_fidelity_ok=runtime_environment_ok,
            )

        try:
            source_sets = int(source_environment_assets.get("source_asset_set_count", 0) or 0)
            fetched_rocks = int(source_environment_assets.get("fetched_rock_mass_count", 0) or 0)
            kenney_cliffs = int(source_environment_assets.get("kenney_cliff_backdrop_count", 0) or 0)
            detailed_trees = int(source_environment_assets.get("detailed_tree_backdrop_count", 0) or 0)
            naturalistic_anchors = int(source_environment_assets.get("naturalistic_anchor_count", 0) or 0)
            terrain_replacements = int(source_environment_assets.get("terrain_replacement_layer_count", 0) or 0)
            backdrop_anchors = int(source_environment_assets.get("backdrop_anchor_count", 0) or 0)
        except Exception:
            source_sets = fetched_rocks = kenney_cliffs = detailed_trees = 0
            naturalistic_anchors = terrain_replacements = backdrop_anchors = 0
        runtime_source_environment = _source_environment_runtime(log_text)
        min_trees = 0 if flags["desert"] else 6
        min_cliffs = 6 if flags["canyon"] else 3
        runtime_source_environment_ok = (
            isinstance(runtime_source_environment, dict)
            and runtime_source_environment.get("source_sets", 0) >= max(source_sets - 1, 6)
            and runtime_source_environment.get("fetched_rocks", 0) >= max(fetched_rocks - 2, 6)
            and runtime_source_environment.get("kenney_cliffs", 0) >= max(kenney_cliffs - 1, min_cliffs)
            and runtime_source_environment.get("detailed_trees", 0) >= max(detailed_trees - 2, min_trees)
            and runtime_source_environment.get("naturalistic_anchors", 0) >= max(naturalistic_anchors - 1, 4)
            and runtime_source_environment.get("terrain_replacements", 0) >= max(terrain_replacements - 1, 4)
            and runtime_source_environment.get("backdrop_anchors", 0) >= max(backdrop_anchors - 2, 8)
        )
        if (
            not isinstance(source_environment_assets, dict)
            or not bool(source_environment_assets.get("enabled"))
            or source_sets < 6
            or fetched_rocks < 8
            or kenney_cliffs < min_cliffs
            or detailed_trees < min_trees
            or naturalistic_anchors < 5
            or terrain_replacements < 5
            or backdrop_anchors < 10
            or not runtime_source_environment_ok
        ):
            fail(
                "missing_source_environment_assets",
                "Generated exterior still relies on low-poly/stage-like backdrop terrain instead of source-bound environmental assets",
                source_environment_assets=source_environment_assets,
                source_asset_set_count=source_sets,
                fetched_rock_mass_count=fetched_rocks,
                kenney_cliff_backdrop_count=kenney_cliffs,
                detailed_tree_backdrop_count=detailed_trees,
                naturalistic_anchor_count=naturalistic_anchors,
                terrain_replacement_layer_count=terrain_replacements,
                backdrop_anchor_count=backdrop_anchors,
                runtime_source_environment_assets=runtime_source_environment,
                runtime_source_environment_assets_ok=runtime_source_environment_ok,
            )

        try:
            hero_material_panels = int(hero_material_shadow_readability.get("hero_material_panel_count", 0) or 0)
            hero_shadow_receivers = int(hero_material_shadow_readability.get("hero_shadow_receiver_count", 0) or 0)
            local_fill_lights = int(hero_material_shadow_readability.get("local_fill_light_count", 0) or 0)
            rim_lights = int(hero_material_shadow_readability.get("rim_light_count", 0) or 0)
            material_contrast = float(hero_material_shadow_readability.get("material_contrast", 0.0) or 0.0)
            exposure_lift = float(hero_material_shadow_readability.get("exposure_lift", 0.0) or 0.0)
        except Exception:
            hero_material_panels = hero_shadow_receivers = local_fill_lights = rim_lights = 0
            material_contrast = exposure_lift = 0.0
        runtime_hero_readability = _hero_material_shadow_runtime(log_text)
        min_panels = 14 if (flags["campsite"] or "cabin" in prompt.lower()) else 10
        min_receivers = 10 if (flags["campsite"] or "cabin" in prompt.lower()) else 8
        runtime_hero_readability_ok = (
            isinstance(runtime_hero_readability, dict)
            and runtime_hero_readability.get("material_panels", 0) >= max(hero_material_panels - 2, min_panels)
            and runtime_hero_readability.get("shadow_receivers", 0) >= max(hero_shadow_receivers - 2, min_receivers)
            and runtime_hero_readability.get("fill_lights", 0) >= max(local_fill_lights, 2)
            and runtime_hero_readability.get("rim_lights", 0) >= max(rim_lights - 1, 2)
            and runtime_hero_readability.get("material_contrast", 0.0) >= max(material_contrast - 0.05, 0.45)
            and runtime_hero_readability.get("exposure_lift", 0.0) >= max(exposure_lift - 0.03, 0.08)
        )
        if (
            not isinstance(hero_material_shadow_readability, dict)
            or not bool(hero_material_shadow_readability.get("enabled"))
            or hero_material_panels < min_panels
            or hero_shadow_receivers < min_receivers
            or local_fill_lights < 2
            or rim_lights < 2
            or material_contrast < 0.48
            or exposure_lift < 0.08
            or not runtime_hero_readability_ok
        ):
            fail(
                "missing_hero_material_shadow_readability",
                "Generated exterior hero surfaces still read as crushed black/flat kit silhouettes instead of locally shaped material and shadow",
                hero_material_shadow_readability=hero_material_shadow_readability,
                hero_material_panel_count=hero_material_panels,
                hero_shadow_receiver_count=hero_shadow_receivers,
                local_fill_light_count=local_fill_lights,
                rim_light_count=rim_lights,
                material_contrast=material_contrast,
                exposure_lift=exposure_lift,
                runtime_hero_material_shadow_readability=runtime_hero_readability,
                runtime_hero_material_shadow_readability_ok=runtime_hero_readability_ok,
            )

        try:
            structural_terrain_tiles = int(structural_scene_fidelity.get("terrain_displacement_tile_count", 0) or 0)
            structural_displacement_layers = int(structural_scene_fidelity.get("terrain_displacement_layer_count", 0) or 0)
            structural_hero_foundations = int(structural_scene_fidelity.get("hero_foundation_count", 0) or 0)
            structural_shadow_casters = int(structural_scene_fidelity.get("shadow_caster_count", 0) or 0)
            structural_material_blends = int(structural_scene_fidelity.get("material_blend_patch_count", 0) or 0)
            structural_light_volumes = int(structural_scene_fidelity.get("light_volume_count", 0) or 0)
            structural_shore_segments = int(structural_scene_fidelity.get("nonplanar_shore_segment_count", 0) or 0)
            structural_min_grid = int(structural_scene_fidelity.get("terrain_tessellation_grid", 0) or 0)
            structural_relief = float(structural_scene_fidelity.get("terrain_relief_m", 0.0) or 0.0)
        except Exception:
            structural_terrain_tiles = structural_displacement_layers = structural_hero_foundations = 0
            structural_shadow_casters = structural_material_blends = structural_light_volumes = 0
            structural_shore_segments = structural_min_grid = 0
            structural_relief = 0.0
        terrain_grid = int(terrain.get("grid", 0) or 0) if isinstance(terrain, dict) else 0
        try:
            terrain_micro = float(terrain.get("micro_relief_m", 0.0) or 0.0) if isinstance(terrain, dict) else 0.0
        except Exception:
            terrain_micro = 0.0
        runtime_structural_scene = _structural_scene_runtime(log_text)
        min_tiles = 10
        min_layers = 4
        min_foundations = 8 if (flags["campsite"] or "cabin" in prompt.lower()) else 5
        min_shadow_casters = 8
        min_material_blends = 10
        min_light_volumes = 2
        min_shore_segments = 6 if flags["water"] else 0
        min_grid = 88
        min_relief = 0.42 if flags["desert"] else 0.52
        min_micro = 0.070 if flags["desert"] else 0.095
        runtime_structural_ok = (
            isinstance(runtime_structural_scene, dict)
            and runtime_structural_scene.get("terrain_tiles", 0) >= max(structural_terrain_tiles - 2, min_tiles)
            and runtime_structural_scene.get("displacement_layers", 0) >= max(structural_displacement_layers - 1, min_layers)
            and runtime_structural_scene.get("hero_foundations", 0) >= max(structural_hero_foundations - 2, min_foundations)
            and runtime_structural_scene.get("shadow_casters", 0) >= max(structural_shadow_casters - 2, min_shadow_casters)
            and runtime_structural_scene.get("material_blends", 0) >= max(structural_material_blends - 3, min_material_blends)
            and runtime_structural_scene.get("light_volumes", 0) >= max(structural_light_volumes - 1, min_light_volumes)
            and runtime_structural_scene.get("shore_segments", 0) >= max(structural_shore_segments - 2, min_shore_segments)
            and runtime_structural_scene.get("terrain_grid", 0) >= max(structural_min_grid, min_grid)
            and runtime_structural_scene.get("relief", 0.0) >= max(structural_relief - 0.03, min_relief)
        )
        if (
            not isinstance(structural_scene_fidelity, dict)
            or not bool(structural_scene_fidelity.get("enabled"))
            or structural_terrain_tiles < min_tiles
            or structural_displacement_layers < min_layers
            or structural_hero_foundations < min_foundations
            or structural_shadow_casters < min_shadow_casters
            or structural_material_blends < min_material_blends
            or structural_light_volumes < min_light_volumes
            or structural_shore_segments < min_shore_segments
            or terrain_grid < max(structural_min_grid, min_grid)
            or relief < max(structural_relief - 0.03, min_relief)
            or terrain_micro < min_micro
            or not runtime_structural_ok
        ):
            fail(
                "missing_structural_scene_fidelity",
                "Generated exterior still reads as flat stage geometry instead of displaced terrain, integrated hero foundations, material blends, and real shadow-casting scene structure",
                structural_scene_fidelity=structural_scene_fidelity,
                terrain_grid=terrain_grid,
                terrain_relief=relief,
                terrain_micro_relief=terrain_micro,
                terrain_displacement_tile_count=structural_terrain_tiles,
                terrain_displacement_layer_count=structural_displacement_layers,
                hero_foundation_count=structural_hero_foundations,
                shadow_caster_count=structural_shadow_casters,
                material_blend_patch_count=structural_material_blends,
                light_volume_count=structural_light_volumes,
                nonplanar_shore_segment_count=structural_shore_segments,
                runtime_structural_scene_fidelity=runtime_structural_scene,
                runtime_structural_scene_fidelity_ok=runtime_structural_ok,
            )

        try:
            tent_shells = int(hero_mesh_material_overhaul.get("tent_shell_count", 0) or 0)
            cabin_cladding = int(hero_mesh_material_overhaul.get("cabin_cladding_count", 0) or 0)
            roof_layers = int(hero_mesh_material_overhaul.get("roof_layer_count", 0) or 0)
            canyon_meshes = int(hero_mesh_material_overhaul.get("canyon_hero_mesh_count", 0) or 0)
            pbr_layers = int(hero_mesh_material_overhaul.get("pbr_material_layer_count", 0) or 0)
            silhouette_masks = int(hero_mesh_material_overhaul.get("lowpoly_silhouette_mask_count", 0) or 0)
        except Exception:
            tent_shells = cabin_cladding = roof_layers = canyon_meshes = pbr_layers = silhouette_masks = 0
        runtime_hero_mesh_material = _hero_mesh_material_runtime(log_text)
        min_tent_shells = 1 if flags["campsite"] and "cabin" not in prompt.lower() else 0
        min_cabin_cladding = 14 if "cabin" in prompt.lower() else 0
        min_roof_layers = 8 if "cabin" in prompt.lower() else 0
        min_canyon_meshes = 8 if flags["desert"] or "canyon" in prompt.lower() else 0
        min_pbr_layers = 6
        min_silhouette_masks = 4 if (flags["campsite"] or "cabin" in prompt.lower() or flags["desert"]) else 2
        runtime_hero_mesh_material_ok = (
            isinstance(runtime_hero_mesh_material, dict)
            and runtime_hero_mesh_material.get("tent_shells", 0) >= max(tent_shells, min_tent_shells)
            and runtime_hero_mesh_material.get("cabin_cladding", 0) >= max(cabin_cladding - 2, min_cabin_cladding)
            and runtime_hero_mesh_material.get("roof_layers", 0) >= max(roof_layers - 1, min_roof_layers)
            and runtime_hero_mesh_material.get("canyon_meshes", 0) >= max(canyon_meshes - 2, min_canyon_meshes)
            and runtime_hero_mesh_material.get("pbr_layers", 0) >= max(pbr_layers - 1, min_pbr_layers)
            and runtime_hero_mesh_material.get("silhouette_masks", 0) >= max(silhouette_masks - 1, min_silhouette_masks)
        )
        if (
            not isinstance(hero_mesh_material_overhaul, dict)
            or not bool(hero_mesh_material_overhaul.get("enabled"))
            or tent_shells < min_tent_shells
            or cabin_cladding < min_cabin_cladding
            or roof_layers < min_roof_layers
            or canyon_meshes < min_canyon_meshes
            or pbr_layers < min_pbr_layers
            or silhouette_masks < min_silhouette_masks
            or not runtime_hero_mesh_material_ok
        ):
            fail(
                "missing_hero_mesh_material_overhaul",
                "Dominant generated heroes still rely on kit silhouettes instead of rebuilt hero meshes with layered PBR material response",
                hero_mesh_material_overhaul=hero_mesh_material_overhaul,
                tent_shell_count=tent_shells,
                cabin_cladding_count=cabin_cladding,
                roof_layer_count=roof_layers,
                canyon_hero_mesh_count=canyon_meshes,
                pbr_material_layer_count=pbr_layers,
                lowpoly_silhouette_mask_count=silhouette_masks,
                runtime_hero_mesh_material_overhaul=runtime_hero_mesh_material,
                runtime_hero_mesh_material_overhaul_ok=runtime_hero_mesh_material_ok,
            )

        try:
            shadowed_lights = int(lighting_shadow_material_field.get("shadowed_spot_light_count", 0) or 0)
            material_surfaces = int(lighting_shadow_material_field.get("material_response_surface_count", 0) or 0)
            shadow_bands = int(lighting_shadow_material_field.get("contact_shadow_band_count", 0) or 0)
            probe_volumes = int(lighting_shadow_material_field.get("local_probe_volume_count", 0) or 0)
            key_fill = float(lighting_shadow_material_field.get("key_fill_ratio", 0.0) or 0.0)
            ambient_ceiling = float(lighting_shadow_material_field.get("ambient_intensity_ceiling", 1.0) or 1.0)
            ssao_target = float(lighting_shadow_material_field.get("ssao_intensity_target", 0.0) or 0.0)
            shadow_bias_target = float(lighting_shadow_material_field.get("shadow_bias_target", 1.0) or 1.0)
            shadow_pcf_target = float(lighting_shadow_material_field.get("shadow_pcf_radius_target", 0.0) or 0.0)
        except Exception:
            shadowed_lights = material_surfaces = shadow_bands = probe_volumes = 0
            key_fill = ssao_target = shadow_pcf_target = 0.0
            ambient_ceiling = shadow_bias_target = 1.0
        runtime_lighting_field = _lighting_shadow_material_field_runtime(log_text)
        runtime_lighting_field_ok = (
            isinstance(runtime_lighting_field, dict)
            and runtime_lighting_field.get("shadowed_lights", 0) >= max(2, shadowed_lights - 1)
            and runtime_lighting_field.get("material_surfaces", 0) >= max(18, material_surfaces - 4)
            and runtime_lighting_field.get("shadow_bands", 0) >= max(12, shadow_bands - 2)
            and runtime_lighting_field.get("probe_volumes", 0) >= max(1, probe_volumes)
            and runtime_lighting_field.get("key_fill", 0.0) >= max(2.2, key_fill - 0.20)
            and runtime_lighting_field.get("ambient", 1.0) <= min(0.62, ambient_ceiling + 0.06)
            and runtime_lighting_field.get("ssao", 0.0) >= max(2.85, ssao_target - 0.15)
            and runtime_lighting_field.get("shadow_bias", 1.0) <= min(0.0018, shadow_bias_target + 0.0003)
            and runtime_lighting_field.get("shadow_pcf", 0.0) >= max(3.35, shadow_pcf_target - 0.20)
        )
        if (
            not isinstance(lighting_shadow_material_field, dict)
            or not bool(lighting_shadow_material_field.get("enabled"))
            or shadowed_lights < 2
            or material_surfaces < 18
            or shadow_bands < 12
            or probe_volumes < 1
            or key_fill < 2.2
            or ambient_ceiling > 0.62
            or ssao_target < 2.85
            or shadow_bias_target > 0.0018
            or shadow_pcf_target < 3.35
            or not runtime_lighting_field_ok
        ):
            fail(
                "missing_lighting_shadow_material_field",
                "Generated exterior lacks scene-wide material response, bounded shadowed lights, ambient clamp, and renderer shadow-field evidence",
                lighting_shadow_material_field=lighting_shadow_material_field,
                shadowed_spot_light_count=shadowed_lights,
                material_response_surface_count=material_surfaces,
                contact_shadow_band_count=shadow_bands,
                local_probe_volume_count=probe_volumes,
                key_fill_ratio=key_fill,
                ambient_intensity_ceiling=ambient_ceiling,
                ssao_intensity_target=ssao_target,
                shadow_bias_target=shadow_bias_target,
                shadow_pcf_radius_target=shadow_pcf_target,
                runtime_lighting_shadow_material_field=runtime_lighting_field,
                runtime_lighting_shadow_material_field_ok=runtime_lighting_field_ok,
            )

        try:
            source_lifts = int(source_readability_balance.get("source_surface_lift_count", 0) or 0)
            backdrop_lifts = int(source_readability_balance.get("backdrop_surface_lift_count", 0) or 0)
            black_splits = int(source_readability_balance.get("black_mass_split_count", 0) or 0)
            albedo_floor = float(source_readability_balance.get("albedo_floor", 0.0) or 0.0)
            nonblack_floor = float(source_readability_balance.get("frame_nonblack_floor", 0.0) or 0.0)
            luma_floor = float(source_readability_balance.get("frame_luma_floor", 0.0) or 0.0)
        except Exception:
            source_lifts = backdrop_lifts = black_splits = 0
            albedo_floor = nonblack_floor = luma_floor = 0.0
        runtime_source_readability = _source_readability_runtime(log_text)
        frame_nonblack = float(image.get("frame_nonblack_fraction", 0.0) or 0.0)
        frame_luma = float(image.get("frame_avg_luma", 0.0) or 0.0)
        runtime_source_readability_ok = (
            isinstance(runtime_source_readability, dict)
            and runtime_source_readability.get("source_surfaces", 0) >= max(18, source_lifts - 6)
            and runtime_source_readability.get("backdrop_surfaces", 0) >= max(6, backdrop_lifts - 3)
            and runtime_source_readability.get("black_splits", 0) >= max(8, black_splits - 4)
            and runtime_source_readability.get("albedo_floor", 0.0) >= max(0.08, albedo_floor - 0.02)
            and runtime_source_readability.get("nonblack_floor", 0.0) >= max(0.78, nonblack_floor - 0.02)
        )
        if (
            not isinstance(source_readability_balance, dict)
            or not bool(source_readability_balance.get("enabled"))
            or source_lifts < 18
            or backdrop_lifts < 6
            or black_splits < 8
            or albedo_floor < 0.08
            or nonblack_floor < (0.82 if flags["moonlight"] else 0.90)
            or luma_floor < (0.09 if flags["moonlight"] else 0.20)
            or frame_nonblack < nonblack_floor
            or frame_luma < luma_floor
            or not runtime_source_readability_ok
        ):
            fail(
                "missing_source_readability_balance",
                "Generated exterior has crushed black source/backdrop masses or lacks a source-material readability rebalance",
                source_readability_balance=source_readability_balance,
                source_surface_lift_count=source_lifts,
                backdrop_surface_lift_count=backdrop_lifts,
                black_mass_split_count=black_splits,
                albedo_floor=albedo_floor,
                frame_nonblack_floor=nonblack_floor,
                frame_luma_floor=luma_floor,
                frame_nonblack_fraction=frame_nonblack,
                frame_avg_luma=frame_luma,
                runtime_source_readability_balance=runtime_source_readability,
                runtime_source_readability_balance_ok=runtime_source_readability_ok,
            )

        if flags["moonlight"] or "storm" in prompt.lower():
            try:
                storm_layers = int(atmosphere_fidelity.get("storm_layer_count", 0) or 0)
                rain_streaks = int(atmosphere_fidelity.get("rain_streak_count", 0) or 0)
                haze_layers = int(atmosphere_fidelity.get("haze_depth_layers", 0) or 0)
            except Exception:
                storm_layers = rain_streaks = haze_layers = 0
            night_control = bool(atmosphere_fidelity.get("night_sky_control"))
            has_runtime_atmosphere = "generative_exterior: atmospheric pass" in log_text
            if (
                not isinstance(atmosphere_fidelity, dict)
                or not bool(atmosphere_fidelity.get("enabled"))
                or (flags["moonlight"] and not night_control)
                or ("storm" in prompt.lower() and (storm_layers < 2 or rain_streaks < 12))
                or haze_layers < 2
                or not has_runtime_atmosphere
            ):
                fail(
                    "missing_atmospheric_time_of_day",
                    "Storm/moonlight prompt lacks authored night-sky, haze, rain, or runtime atmosphere evidence",
                    atmosphere_fidelity=atmosphere_fidelity,
                    runtime_atmosphere=has_runtime_atmosphere,
                )

        camera_role = str(shot.get("camera_role", "") if isinstance(shot, dict) else "").lower()
        has_shot_camera_contract = ("closer" in camera_role or "balanced" in camera_role) and "hero" in camera_role
        has_runtime_shot_camera = "generative_exterior: shot camera pass" in log_text
        if not (has_shot_camera_contract and has_runtime_shot_camera):
            fail(
                "missing_shot_camera_pass",
                "Generated exterior lacks the closer hero-scale camera profile required to avoid tiny staged blockouts",
                shot=shot,
                runtime_shot_camera=has_runtime_shot_camera,
            )

        try:
            occlusion_ribbons = int(occlusion.get("ground_shadow_ribbon_count", 0) or 0)
            contact_shadow_strength = float(occlusion.get("contact_shadow_strength", 0.0) or 0.0)
            terrain_creases = int(surface_detail.get("terrain_crease_count", 0) or 0)
            pebbles = int(surface_detail.get("pebble_count", 0) or 0)
            wet_glints = int(surface_detail.get("wet_glint_count", 0) or 0)
            shore_foam = int(surface_detail.get("shore_foam_segment_count", 0) or 0)
        except Exception:
            occlusion_ribbons = terrain_creases = pebbles = wet_glints = shore_foam = 0
            contact_shadow_strength = 0.0
        has_runtime_occlusion = "generative_exterior: created occlusion layering" in log_text
        has_runtime_surface = "generative_exterior: created surface detail" in log_text
        if (
            occlusion_ribbons < 5
            or contact_shadow_strength < 0.45
            or terrain_creases < 4
            or pebbles < 16
            or (flags["water"] and (shore_foam < 3 or wet_glints < 3))
            or not (has_runtime_occlusion and has_runtime_surface)
        ):
            fail(
                "missing_occlusion_surface_layers",
                "Scene lacks layered ground occlusion, terrain creases, micro surface detail, or shore wet/foam integration",
                occlusion=occlusion,
                surface_detail=surface_detail,
                runtime_occlusion=has_runtime_occlusion,
                runtime_surface=has_runtime_surface,
            )

        try:
            deep_contact_patches = int(image_contact_occlusion.get("deep_contact_patch_count", 0) or 0)
            target_dark_contact = float(image_contact_occlusion.get("target_dark_contact_fraction", 0.002) or 0.002)
            target_dark_contact_area = float(image_contact_occlusion.get("target_dark_contact_area_fraction", 0.004) or 0.004)
        except Exception:
            deep_contact_patches = 0
            target_dark_contact = 0.002
            target_dark_contact_area = 0.004
        has_runtime_image_contact = "generative_exterior: created image contact occluders" in log_text
        if (
            not isinstance(image_contact_occlusion, dict)
            or not bool(image_contact_occlusion.get("enabled"))
            or deep_contact_patches < 8
            or not has_runtime_image_contact
        ):
            fail(
                "missing_image_contact_occlusion_pass",
                "Scene lacks a deep contact-occlusion pass for visually grounded props",
                image_contact_occlusion=image_contact_occlusion,
                deep_contact_patch_count=deep_contact_patches,
                runtime_image_contact_occlusion=has_runtime_image_contact,
            )

        if flags["water"]:
            try:
                foam_lace = int(water_shore_integration.get("foam_lace_segment_count", 0) or 0)
                shoreline_ripples = int(water_shore_integration.get("shoreline_ripple_count", 0) or 0)
                wetline_bands = int(water_shore_integration.get("wetline_band_count", 0) or 0)
                reflection_glints = int(water_shore_integration.get("reflection_glint_count", 0) or 0)
                submerged_rocks = int(water_shore_integration.get("submerged_edge_rock_count", 0) or 0)
            except Exception:
                foam_lace = shoreline_ripples = wetline_bands = reflection_glints = submerged_rocks = 0
            has_runtime_water_shore = "generative_exterior: created water shore integration" in log_text
            if (
                not isinstance(water_shore_integration, dict)
                or not bool(water_shore_integration.get("enabled"))
                or foam_lace < 8
                or shoreline_ripples < 8
                or wetline_bands < 3
                or reflection_glints < 6
                or submerged_rocks < 4
                or not has_runtime_water_shore
            ):
                fail(
                    "missing_water_shore_integration_pass",
                    "Water prompt lacks authored shoreline foam, ripples, wetline bands, reflection glints, and submerged-edge grounding",
                    water_shore_integration=water_shore_integration,
                    runtime_water_shore_integration=has_runtime_water_shore,
                )

        try:
            penumbra_patches = int(soft_occlusion.get("penumbra_patch_count", 0) or 0)
            gradient_layers = int(soft_occlusion.get("contact_gradient_layer_count", 0) or 0)
            hero_anchors = int(soft_occlusion.get("hero_anchor_count", 0) or 0)
        except Exception:
            penumbra_patches = gradient_layers = hero_anchors = 0
        has_runtime_soft_occlusion = "generative_exterior: created soft contact occlusion" in log_text
        if (
            not isinstance(soft_occlusion, dict)
            or not bool(soft_occlusion.get("enabled"))
            or penumbra_patches < 12
            or gradient_layers < 2
            or hero_anchors < 6
            or not has_runtime_soft_occlusion
        ):
            fail(
                "missing_soft_occlusion_pass",
                "Scene lacks broad soft contact-occlusion penumbra around props and hero anchors",
                soft_occlusion=soft_occlusion,
                runtime_soft_occlusion=has_runtime_soft_occlusion,
            )

        try:
            foreground_occluders = int(world_geometry.get("foreground_occluder_count", 0) or 0)
            depth_bands = int(shot.get("depth_band_count", world_geometry.get("depth_band_count", 0)) or 0)
            ridge_layers = int(world_geometry.get("ridge_layer_count", 0) or 0)
            shoreline_segments = int(world_geometry.get("shoreline_segment_count", 0) or 0)
        except Exception:
            foreground_occluders = depth_bands = ridge_layers = shoreline_segments = 0
        has_runtime_world = "generative_exterior: created world geometry" in log_text
        has_runtime_foreground = "generative_exterior: created foreground occluder" in log_text
        if (
            foreground_occluders < 2
            or depth_bands < 4
            or ridge_layers < 2
            or (flags["water"] and shoreline_segments < 2)
            or not (has_runtime_world and has_runtime_foreground)
        ):
            fail(
                "missing_world_depth_geometry",
                "Generated exterior lacks foreground/midground/shore/horizon world-geometry depth evidence",
                world_geometry=world_geometry,
                shot=shot,
                runtime_world=has_runtime_world,
                runtime_foreground=has_runtime_foreground,
            )

        canyon_like = flags["canyon"] or (
            flags["desert"]
            and "canyon" in str((ir.get("director") or {}).get("scene_type", "")).lower()
        )
        if canyon_like:
            try:
                canyon_wall_layers = int(world_geometry.get("canyon_wall_layers", 0) or 0)
                talus_clusters = int(world_geometry.get("talus_cluster_count", 0) or 0)
                strata_layers = int(world_geometry.get("red_rock_strata_layers", 0) or 0)
                erosion_ridges = int(geometry_realism.get("cliff_erosion_ridge_count", 0) or 0)
                strata_breakup = int(geometry_realism.get("strata_breakup_count", 0) or 0)
            except Exception:
                canyon_wall_layers = talus_clusters = strata_layers = 0
                erosion_ridges = strata_breakup = 0
            has_runtime_canyon = "generative_exterior: created canyon wall" in log_text
            if canyon_wall_layers < 4 or talus_clusters < 8 or strata_layers < 4 or not has_runtime_canyon:
                fail(
                    "desert_canyon_blockout",
                    "Canyon prompt lacks canyon-wall, talus, and red-rock strata evidence",
                    world_geometry=world_geometry,
                    runtime_canyon=has_runtime_canyon,
                )
            has_runtime_cliff_detail = "generative_exterior: created cliff erosion detail" in log_text
            if (
                not isinstance(geometry_realism, dict)
                or not bool(geometry_realism.get("enabled"))
                or erosion_ridges < 10
                or strata_breakup < 8
                or not has_runtime_cliff_detail
            ):
                fail(
                    "planar_cliff_geometry",
                    "Canyon walls still read as planar blockout geometry without erosion/strata breakup evidence",
                    geometry_realism=geometry_realism,
                    runtime_cliff_detail=has_runtime_cliff_detail,
                )
            assets = _asset_counts(ir)
            if assets["cliffs"] < 4:
                fail(
                    "missing_catalog_cliff_assets",
                    "Canyon prompt lacks real catalog cliff assets to break up procedural wall silhouettes",
                    assets=assets,
                )

        if flags["desert"] or flags["canyon"]:
            assets = _asset_counts(ir)
            if assets["pines"] > 2 or assets["trees"] > 8:
                fail(
                    "tree_heavy_desert_staging",
                    "Desert/canyon scene reads as a generic forest campsite because tree assets dominate the flanks",
                    assets=assets,
                )

        if image:
            if image["ground_vertical_detail"] < 0.010:
                fail("low_ground_surface_detail", "Ground band has too little vertical/detail variation", image=image)
            elif (
                image["dark_contact_fraction"] < target_dark_contact
                and image.get("dark_contact_area_fraction", 0.0) < target_dark_contact_area
            ):
                fail(
                    "weak_contact_shadow_image_metric",
                    "Rendered ground band has too little hard dark contact-shadow evidence",
                    image=image,
                    image_contact_occlusion=image_contact_occlusion,
                    target_dark_contact_fraction=target_dark_contact,
                    target_dark_contact_area_fraction=target_dark_contact_area,
                )
        else:
            warn("image_metrics_skipped", "PNG/Pillow unavailable; only IR/runtime graphics evidence was checked")

    metrics: dict[str, Any] = {}
    if image:
        metrics["image"] = image
    if frame_pipeline.get("report_present") or frame_pipeline.get("missing"):
        metrics["frame_pipeline"] = frame_pipeline

    return {
        "prompt": prompt,
        "passed": not failures,
        "failures": failures,
        "warnings": warnings,
        "metrics": metrics,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Generated-scene graphics fidelity gate")
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
        required = {
            "missing_terrain_relief",
            "missing_contact_grounding",
            "missing_material_pass",
            "missing_runtime_graphics_evidence",
        }
        got = {f["code"] for f in report["failures"]}
        missing = sorted(required - got)
        if report["passed"] or missing:
            print(f"expected known-bad graphics failure codes missing: {missing}", file=sys.stderr)
            return 2
        return 0
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
