#!/usr/bin/env python3
"""Graphics-fidelity gate for generated exterior stills.

This complements scene_quality_gate.py. It does not claim an image is AAA; it
rejects the obvious blockout class: flat generated exteriors with disconnected
props, no terrain/contact/material/shader pass, weak occlusion layering, weak
surface material breakup, weak texture-backed material coverage, and no runtime
evidence that the high-quality exterior graphics path ran.
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
        "ground_box": box,
        "ground_edge_density": round(edge_sum / samples, 4),
        "ground_vertical_detail": round(vertical_sum / samples, 4),
        "dark_contact_fraction": round(dark_contact / samples, 4),
        "dark_contact_area_fraction": round(dark_contact_area / samples, 4),
        "sample_count": samples,
    }


def evaluate(prompt: str, ir: dict[str, Any], png: Path | None, log_text: str) -> dict[str, Any]:
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
    image = _image_metrics(png)

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

    return {
        "prompt": prompt,
        "passed": not failures,
        "failures": failures,
        "warnings": warnings,
        "metrics": {"image": image} if image else {},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Generated-scene graphics fidelity gate")
    ap.add_argument("--prompt", required=True)
    ap.add_argument("--ir", required=True, type=Path)
    ap.add_argument("--png", type=Path)
    ap.add_argument("--log", type=Path)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--expect-fail", action="store_true")
    args = ap.parse_args()

    ir = _load_json(args.ir)
    log_text = _read_log(args.log)
    report = evaluate(args.prompt, ir, args.png, log_text)

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
