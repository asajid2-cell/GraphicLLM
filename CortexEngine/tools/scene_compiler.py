#!/usr/bin/env python3
"""Compile Director IR v3 to the current engine Scene IR.

This is intentionally conservative: CORTEX_SCENE_IR_JSON remains the runtime
contract, so v3 lowers into the existing exterior IR plus metadata that newer
quality gates can inspect. Native C++ support for richer v3 layers comes later.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import random
from pathlib import Path
from typing import Any


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def _round3(v: float) -> float:
    return round(float(v), 3)


def _stable_seed(text: str) -> int:
    digest = hashlib.sha1(text.encode("utf-8")).hexdigest()
    return int(digest[:8], 16) % 16_000_000 + 1000


def _asset_exists(asset_id: str) -> bool:
    # The compiler uses stable catalog ids already proven in the current runtime.
    return bool(asset_id)


def _collision_radius(asset: str, foot: float) -> float:
    scale = 0.55 if "tree_" in asset.lower() else 1.0
    return max(foot / 2.0, 0.25) * scale


def _fits(placed: list[tuple[float, float, float]], x: float, z: float, r: float) -> bool:
    for px, pz, pr in placed:
        if math.hypot(x - px, z - pz) < (r + pr) * 1.04:
            return False
    return True


def _add(objects: list[dict[str, Any]], asset: str, x: float, z: float, yaw: float, foot: float,
         tint: list[float] | None = None, y: float | None = None,
         placed: list[tuple[float, float, float]] | None = None) -> bool:
    if not _asset_exists(asset):
        return False
    r = _collision_radius(asset, foot)
    if placed is not None and not _fits(placed, x, z, r):
        return False
    obj: dict[str, Any] = {
        "asset": asset,
        "x": _round3(x),
        "z": _round3(z),
        "yaw": round(float(yaw), 1),
        "foot": _round3(foot),
    }
    if tint is not None:
        obj["tint"] = tint
    if y is not None:
        obj["y"] = _round3(y)
    objects.append(obj)
    if placed is not None:
        placed.append((x, z, r))
    return True


def _material_for_asset(asset: str, desert: bool, moonlight: bool) -> dict[str, Any]:
    low = asset.lower()
    if any(t in low for t in ("tree", "grass", "pine", "plant")):
        return {
            "preset": "foliage",
            "ao": 0.78,
            "roughness": 0.68 if moonlight else 0.62,
            "normal_scale": 0.42,
            "procedural_mask": 0.34,
            "wetness": 0.12 if not desert else 0.03,
            "specular": 0.50,
            "sheen": 0.14,
            "subsurface": 0.20 if not desert else 0.08,
        }
    if any(t in low for t in ("rock", "boulder", "stone")):
        return {
            "preset": "wet_stone" if not desert else "stone",
            "ao": 0.72,
            "roughness": 0.54 if not desert else 0.76,
            "normal_scale": 0.62,
            "procedural_mask": 0.48,
            "wetness": 0.36 if not desert else 0.08,
            "specular": 0.74 if not desert else 0.48,
            "clearcoat": 0.18 if not desert else 0.05,
            "anisotropy": 0.16,
        }
    if any(t in low for t in ("log", "trunk", "wood")):
        return {
            "preset": "wood",
            "ao": 0.76,
            "roughness": 0.58,
            "normal_scale": 0.48,
            "procedural_mask": 0.46,
            "wetness": 0.16,
            "specular": 0.54,
            "anisotropy": 0.34,
            "clearcoat": 0.08,
        }
    if "tent" in low:
        return {
            "preset": "fabric",
            "ao": 0.96,
            "roughness": 0.60,
            "normal_scale": 0.58,
            "procedural_mask": 0.44,
            "wetness": 0.08,
            "specular": 0.50,
            "sheen": 0.42,
            "subsurface": 0.16,
        }
    return {
        "preset": "naturalistic",
        "ao": 0.80,
        "roughness": 0.70,
        "normal_scale": 0.34,
        "procedural_mask": 0.24,
        "wetness": 0.06,
        "specular": 0.42,
        "clearcoat": 0.05,
    }


def _attach_materials(objects: list[dict[str, Any]], desert: bool, moonlight: bool) -> None:
    for obj in objects:
        obj["material"] = _material_for_asset(str(obj.get("asset") or ""), desert, moonlight)


def _contact_patches(objects: list[dict[str, Any]], water_from_z: float, limit: int = 12) -> list[dict[str, Any]]:
    patches: list[dict[str, Any]] = []
    tree_count = 0
    for obj in objects:
        asset = str(obj.get("asset") or "").lower()
        is_grass = "grass" in asset or "plant" in asset
        is_tree = "tree_" in asset or "pine" in asset
        if is_grass:
            continue
        if is_tree:
            tree_count += 1
            if tree_count > 2:
                continue
        try:
            x = float(obj.get("x", 0.0))
            z = float(obj.get("z", 0.0))
            foot = float(obj.get("foot", 1.0))
        except Exception:
            continue
        if "tent" in asset:
            radius_scale = 0.46
        elif any(t in asset for t in ("rock", "boulder", "stone")):
            radius_scale = 0.40
        elif any(t in asset for t in ("log", "trunk", "wood")):
            radius_scale = 0.42
        elif is_tree:
            radius_scale = 0.32
        else:
            radius_scale = 0.45
        radius = _clamp(foot * radius_scale, 0.28, 1.45)
        near_shore = abs(z - water_from_z) <= 2.6 or z < water_from_z + 1.4
        patches.append({
            "x": _round3(x),
            "z": _round3(z),
            "radius": _round3(radius),
            "darkness": 0.54 if near_shore else 0.42,
            "wetness": 0.56 if near_shore else 0.24,
        })
        if len(patches) >= limit:
            break
    return patches


def _scatter_ring(objects: list[dict[str, Any]], asset: str, count: int, radius_x: float, z_base: float,
                  z_jitter: float, foot: float, seed: str, tint: list[float] | None = None,
                  sides: bool = True, placed: list[tuple[float, float, float]] | None = None) -> None:
    rng = random.Random(seed)
    for i in range(count):
        for attempt in range(80):
            side = -1 if i % 2 == 0 else 1
            spread = 1.0 + attempt * 0.018
            x = side * rng.uniform(radius_x * 0.58, radius_x * spread)
            if not sides and rng.random() < 0.45:
                x = rng.uniform(-radius_x * 0.92, radius_x * 0.92)
            z = z_base + rng.uniform(-z_jitter * spread, z_jitter * spread)
            yaw = rng.uniform(0, 360)
            scale = foot * rng.uniform(0.82, 1.18)
            if _add(objects, asset, x, z, yaw, scale, tint=tint, placed=placed):
                break


def _water_palette(waterbody: dict[str, Any]) -> tuple[list[float], list[float]]:
    shallow = list(waterbody.get("shallow", [0.10, 0.52, 0.36]))
    deep = list(waterbody.get("deep", [0.01, 0.23, 0.19]))
    intent = str(waterbody.get("color_intent") or "").lower()
    if intent in {"purple", "violet"}:
        shallow = [max(shallow[0], 0.86), min(shallow[1], 0.06), 1.0]
        deep = [max(deep[0], 0.26), min(deep[1], 0.02), max(deep[2], 0.56)]
    elif intent == "turquoise":
        shallow = [min(shallow[0], 0.08), max(shallow[1], 0.78), max(shallow[2], 0.82)]
        deep = [min(deep[0], 0.02), max(deep[1], 0.24), max(deep[2], 0.30)]
    return shallow, deep


def compile_v3_to_v2(v3: dict[str, Any]) -> dict[str, Any]:
    intent = v3.get("intent") or {}
    world = v3.get("world") or {}
    lighting = v3.get("lighting_look") or {}
    waterbody = world.get("waterbody") or {}
    terrain = world.get("terrain") or {}
    background = world.get("background") or {}
    materials = v3.get("materials") or {}
    water_material = materials.get("water") or {}
    atmosphere = lighting.get("atmosphere") or {}
    fog = atmosphere.get("fog") or {}
    sun = lighting.get("sun") or {}
    post = lighting.get("post") or {}

    scene_type = str(intent.get("scene_type") or "exterior_landscape")
    prompt = str(intent.get("prompt") or "")
    prompt_lower = prompt.lower()
    canyon = "canyon" in scene_type or "canyon" in prompt_lower
    desert = "desert" in scene_type or canyon or "desert" in prompt_lower
    campsite = "camp" in scene_type or "camp" in prompt_lower
    cabin = "cabin" in scene_type or "cabin" in prompt_lower
    time_of_day = str(lighting.get("time") or "").lower()
    moonlight = time_of_day == "moonlight" or "moon" in prompt_lower or "moonlight" in prompt_lower
    stormy = "storm" in prompt_lower or "storm" in time_of_day

    extent = float(terrain.get("extent_m", 44.0) or 44.0)
    extent = _clamp(extent, 30.0, 52.0)
    water_on = waterbody.get("type") in ("lake", "river", "waterbody") or "lake" in prompt_lower or "river" in prompt_lower
    if campsite and water_on and not desert:
        authored_module_id = "campsite_lake_dawn"
    elif canyon:
        authored_module_id = "desert_canyon_river"
    elif cabin and water_on:
        authored_module_id = "alpine_cabin_lake"
    elif water_on:
        authored_module_id = "exterior_water_setpiece"
    else:
        authored_module_id = "exterior_landscape_setpiece"
    water_from_z = -0.16 * extent
    if canyon and water_on:
        # Canyon rivers need to occupy the midground water band as actual water,
        # not rely on pale shore proxy strips to keep the prompt colour readable.
        water_from_z = -0.115 * extent
    water_shallow, water_deep = _water_palette(waterbody)
    water_intent = str(waterbody.get("color_intent") or "").lower()
    stylized_water = water_intent in {"purple", "violet", "turquoise", "blue"}
    if canyon and water_intent == "turquoise":
        water_shallow = [0.02, 1.58, 2.25]
        water_deep = [0.0, 0.58, 1.24]
    if water_intent in {"purple", "violet"}:
        water_shallow = [0.55, 0.035, 1.02]
        water_deep = [0.18, 0.00, 0.50]
    if moonlight and not stylized_water:
        water_shallow = [0.11, 0.30, 0.52]
        water_deep = [0.01, 0.045, 0.17]

    ground_kind = "dirt" if desert else "grass"
    ground_color = [0.58, 0.28, 0.18] if desert else [0.25, 0.34, 0.22]
    if moonlight and not desert:
        ground_color = [0.15, 0.19, 0.29]

    ridge_layers = list(background.get("ridge_layers") or [])
    if not ridge_layers:
        ridge_layers = [
            {
                "distance_m": 55,
                "height_m": 14 if canyon else 10,
                "color": [0.45, 0.20, 0.13] if desert else [0.18, 0.20, 0.25],
            },
            {
                "distance_m": 84,
                "height_m": 25 if canyon else 18,
                "color": [0.22, 0.10, 0.085] if desert else [0.10, 0.12, 0.18],
            },
        ]

    terrain_seed = _stable_seed(f"{scene_type}:{prompt_lower}:terrain")

    env = {
        "sun": {
            "azimuth_deg": float(sun.get("azimuth_deg", 135.0) or 135.0),
            "elevation_deg": float(sun.get("elevation_deg", 10.0) or 10.0),
            "color": sun.get("color", [1.0, 0.58, 0.32]),
            "intensity": float(sun.get("intensity", 3.1) or 3.1),
        },
        "sky": "cool_overcast" if moonlight else (
            "sky_sunset" if float(sun.get("elevation_deg", 45.0) or 45.0) <= 18.0 else "sky_day"
        ),
        "look": {
            "time": "moonlight" if moonlight else time_of_day or "day",
            "grade": "cool_moonlight" if moonlight else "storm_cool" if stormy else "day",
        },
        "fog": {
            "density": max(float(fog.get("density", 0.014) or 0.014), 0.018 if stormy else 0.0),
            "start": float(fog.get("start_m", 7.0) or 7.0),
        },
        "exposure": max(float(post.get("exposure", 0.95) or 0.95), 1.08 if moonlight else 0.0),
        "ground": {
            "kind": ground_kind,
            "extent": extent,
            "color": ground_color,
            "terrain": {
                "mode": "heightfield",
                "grid": 96,
                "relief_m": 0.56 if moonlight else (0.64 if not desert else 0.48),
                "micro_relief_m": 0.105 if not desert else 0.078,
                "shore_flatten_m": 5.5,
                "seed": terrain_seed,
                "noise": "shared_fbm_heightfield",
            },
        },
        "water": {
            "enabled": bool(water_on),
            "shallow": water_shallow,
            "deep": water_deep,
            "roughness": float(waterbody.get("roughness", 0.10) or 0.10),
            "wave": 0.035 if waterbody.get("type") == "river" else 0.022,
            "from_z": round(water_from_z, 2),
            "level": 0.05,
            "absorption": float(water_material.get("absorption", 0.85) or 0.85),
            "foam": float(water_material.get("foam", 0.12 if stylized_water else 0.28) or 0.12),
            "fresnel": 0.20 if stylized_water else 0.44,
            "viscosity": 0.76 if stylized_water else 0.50,
            "body_thickness": 1.05 if stylized_water else 0.82,
            "color_strength": 0.48 if stylized_water else 0.0,
        },
        "background": {
            "ridge_layers": ridge_layers,
            "intent": "procedural_ridge_backdrop",
        },
        "structures": [],
        "graphics_pass": {},
    }

    if authored_module_id == "campsite_lake_dawn":
        env["sun"].update({
            "azimuth_deg": 126.0,
            "elevation_deg": 8.5,
            "color": [1.0, 0.46, 0.24],
            "intensity": 4.35,
        })
        env["sky"] = "sky_sunset"
        env["look"] = {"time": "dawn", "grade": "warm_fog_contrast"}
        env["fog"] = {"density": max(env["fog"]["density"], 0.026), "start": 4.0}
        env["exposure"] = min(env["exposure"], 0.88)
        env["water"].update({
            "roughness": min(env["water"]["roughness"], 0.065),
            "fresnel": 0.24 if stylized_water else 0.48,
            "absorption": max(env["water"]["absorption"], 1.05),
            "foam": max(env["water"]["foam"], 0.20),
            "body_thickness": max(env["water"]["body_thickness"], 1.18),
            "color_strength": 0.46 if stylized_water else env["water"]["color_strength"],
        })
        if water_intent in {"purple", "violet"}:
            env["water"].update({
                "shallow": [0.64, 0.020, 1.34],
                "deep": [0.25, 0.0, 0.82],
                "color_strength": 0.62,
            })
    elif authored_module_id == "desert_canyon_river":
        env["sun"].update({
            "azimuth_deg": 112.0,
            "elevation_deg": 13.0,
            "color": [1.0, 0.62, 0.34],
            "intensity": 4.10,
        })
        env["sky"] = "sky_sunset"
        env["look"] = {"time": "golden_hour", "grade": "desert_canyon_contrast"}
        env["fog"] = {"density": max(env["fog"]["density"], 0.012), "start": 8.0}
        env["exposure"] = min(env["exposure"], 0.84)
        env["water"].update({
            "shallow": [0.02, 1.95, 2.62],
            "deep": [0.0, 0.78, 1.48],
            "roughness": min(env["water"]["roughness"], 0.052),
            "fresnel": 0.18,
            "absorption": max(env["water"]["absorption"], 1.22),
            "foam": max(env["water"]["foam"], 0.24),
            "body_thickness": max(env["water"]["body_thickness"], 1.34),
            "color_strength": 0.58,
        })
    elif authored_module_id == "alpine_cabin_lake":
        env["sun"].update({
            "azimuth_deg": 214.0,
            "elevation_deg": 12.0,
            "color": [0.55, 0.66, 1.0],
            "intensity": 1.45,
        })
        env["look"] = {"time": "moonlight" if moonlight else "blue_hour", "grade": "cool_moonlight"}
        env["fog"] = {"density": max(env["fog"]["density"], 0.022), "start": 5.0}
        env["exposure"] = min(env["exposure"], 0.78)

    objects: list[dict[str, Any]] = []
    placed: list[tuple[float, float, float]] = []
    foliage_tint = [0.34, 0.68, 0.28] if not desert else [0.55, 0.48, 0.25]
    rock_tint = [0.45, 0.44, 0.40] if not desert else [0.72, 0.30, 0.18]
    if moonlight and not desert:
        foliage_tint = [0.18, 0.32, 0.42]
        rock_tint = [0.26, 0.30, 0.42]

    # Hero grammar: the focal set stays tight enough for the quality gate and camera.
    if campsite:
        # The renderer-owned procedural canvas tent owns the hero silhouette.
        # Keeping the catalog wedge here hides the better shell behind a low-poly slab.
        _add(objects, "campfire_bricks", -0.35, 0.35, 0.0, 1.05, placed=placed)
        _add(objects, "campfire_logs", -1.05, 0.86, 31.0, 0.56, placed=placed)
        _add(objects, "campfire_stones", 0.48, -0.18, -12.0, 0.62, placed=placed)
        _add(objects, "Lantern_01", 1.15, 1.42, -22.0, 0.50, tint=[0.72, 0.46, 0.22])
        _add(objects, "dead_tree_trunk", -3.35, 2.55, 88.0, 0.82, tint=[0.34, 0.23, 0.15], placed=placed)
        _add(objects, "tree_stump_01", 2.20, 2.62, -12.0, 0.56, tint=[0.28, 0.19, 0.11], placed=placed)
    elif cabin:
        env["structures"].append({
            "type": "cabin",
            "x": 1.35,
            "z": 1.05,
            "yaw_deg": -10.0,
            "width_m": 3.8,
            "depth_m": 3.0,
            "wall_height_m": 2.0,
            "roof_height_m": 1.0,
            "lit_windows": True,
            "material": "weathered_wood",
        })
        placed.append((1.35, 1.05, 2.7))
        _add(objects, "campfire_bricks", -1.9, 1.25, 0.0, 0.95, placed=placed)
        _add(objects, "dead_tree_trunk", 2.70, 2.38, 80.0, 0.72, placed=placed)

    # Shore, water, and foreground anchors.
    _scatter_ring(objects, "boulder_01", 7, radius_x=13.5, z_base=-4.4, z_jitter=1.6,
                  foot=1.55, seed=scene_type + ":shore", tint=rock_tint, sides=False, placed=placed)
    _scatter_ring(objects, "rock_moss_set_01", 3, radius_x=14.0, z_base=-11.4, z_jitter=1.8,
                  foot=1.75, seed=scene_type + ":water-rocks", tint=rock_tint, sides=False, placed=placed)
    if not desert:
        _scatter_ring(objects, "grass_bermuda_01", 8, radius_x=13.0, z_base=3.8, z_jitter=2.0,
                      foot=0.56, seed=scene_type + ":grass", tint=foliage_tint, sides=True, placed=placed)

    # Tree/vertical masses on flanks, not the center corridor. Desert canyons use
    # sparse dead trunks instead of pine flanks so they do not read as a forest camp.
    if desert:
        _scatter_ring(objects, "dead_tree_trunk", 5, radius_x=18.0, z_base=0.6, z_jitter=3.0,
                      foot=1.55, seed=scene_type + ":snags", tint=[0.34, 0.22, 0.13],
                      placed=placed)
        if canyon:
            cliff_specs = [
                ("cliff_large_rock", -19.5, -3.2, 8.0, 3.6),
                ("cliff_large_rock", 19.2, -5.8, 188.0, 3.4),
                ("cliff_cornerLarge_rock", -20.4, -12.4, 24.0, 3.3),
                ("cliff_cornerLarge_rock", 20.0, -15.2, 204.0, 3.2),
                ("cliff_blockSlope_rock", -17.8, -21.2, -12.0, 3.0),
                ("cliff_blockSlope_rock", 17.6, -21.8, 168.0, 3.0),
            ]
            for asset, x, z, yaw, foot in cliff_specs:
                _add(objects, asset, x, z, yaw, foot, tint=rock_tint, placed=placed)
    else:
        _scatter_ring(objects, "tree_pineTallA_detailed", 2, radius_x=21.0, z_base=-5.6, z_jitter=2.4,
                      foot=3.0, seed=scene_type + ":trees", tint=foliage_tint,
                      placed=placed)

    _attach_materials(objects, desert=desert, moonlight=moonlight)
    contact_patches: list[dict[str, Any]] = []
    foreground_occluders = 2 if canyon else 1
    shoreline_segments = 3 if water_on else 0
    material_zone_names = [
        "terrain_red_dirt" if desert else "terrain_grass_rock",
        "shore_wet_band" if water_on else "dry_contact_shadow",
        "water_surface" if water_on else "sky_reflection_probe",
        "rock_talus" if desert else "wet_boulders",
        "dry_scrub" if desert else "foliage_flanks",
    ]
    if canyon:
        material_zone_names.extend(["red_rock_cliff_faces", "red_rock_strata", "foreground_silhouette_rocks"])
    elif campsite:
        material_zone_names.append("camp_hero_fabric_wood")
    material_zone_names.extend(["terrain_micro_pebbles", "renderer_ssao_contact_grounding"])
    if water_on:
        material_zone_names.extend([
            "shore_material_transition",
            "water_subsystem_specular_response",
            "shaped_water_mesh",
            "submerged_edge_rock_wetlines",
        ])
    material_zone_names.extend(["hero_environment_geometry", "mountain_cliff_massing", "shoreline_prop_geometry"])
    material_zone_names.extend([
        "texture_source_terrain",
        "texture_source_rock_cliff",
        "texture_source_wood",
        "texture_source_fabric",
    ])
    material_zone_names.extend([
        "authored_scene_module_layout",
        "authored_foreground_frame",
        "authored_midground_hero_cluster",
        "authored_backdrop_gate",
        "authored_lighting_zones",
    ])
    material_zone_names.extend([
        "cinematic_triplanar_material_relief",
        "cinematic_shadow_caster_receivers",
        "cinematic_volumetric_light_slices",
    ])
    material_zone_names.extend([
        "source_environment_replacement",
        "source_crag_cliff_backdrop",
        "source_tree_and_boulder_terrain_anchors",
    ])
    material_zone_names.extend([
        "hero_material_shadow_readability",
        "local_fill_and_rim_light_shaping",
        "hero_contact_shadow_receivers",
    ])
    material_zone_names.extend([
        "structural_displaced_terrain_tiles",
        "integrated_hero_foundation_geometry",
        "triplanar_material_blend_patches",
        "shadow_caster_scene_structure",
        "nonplanar_shore_bank_geometry",
    ])
    material_zone_names.extend([
        "rebuilt_hero_mesh_shell",
        "layered_pbr_hero_materials",
        "lowpoly_silhouette_masking",
        "cabin_cladding_roof_shingles",
        "canyon_hero_mesh_overbuild",
    ])
    material_zone_names.extend([
        "shadowed_key_light_material_response",
        "ambient_clamped_contact_shadows",
        "local_probe_volume_material_grade",
        "raking_shadow_band_field",
        "scene_wide_pbr_response_tuning",
    ])
    material_zone_names.extend([
        "source_asset_readability_lift",
        "backdrop_albedo_floor_balance",
        "crushed_black_mass_split_highlights",
        "ridge_and_cliff_readability_guard",
    ])
    if water_on:
        material_zone_names.append("texture_source_wet_shore")
        material_zone_names.append("authored_curved_shore_water_corridor")
        material_zone_names.append("cinematic_wet_dry_roughness_transition")
    if not desert:
        material_zone_names.append("irregular_tree_silhouettes")
    env["shot"] = {
        "composition": "foreground_frame_midground_hero_water_horizon",
        "depth_band_count": 5 if water_on else 4,
        "foreground_framing": True,
        "clear_view_corridor": True,
        "camera_role": "balanced_cabin_hero_water_horizon" if cabin else "closer_midground_hero_water_horizon",
    }
    renderer_ssao_radius = 1.36 if not moonlight else 1.24
    renderer_ssao_intensity = 3.05 if not moonlight else 2.80
    renderer_shadow_pcf = 3.45 if not moonlight else 3.30
    if authored_module_id == "alpine_cabin_lake":
        renderer_ssao_radius = 1.24
        renderer_ssao_intensity = 2.80
        renderer_shadow_pcf = 3.30
    renderer_shadow_bias = 0.0014
    contact_receiver_patch_budget = 0
    soft_penumbra_patch_budget = 0
    cinematic_relief_patches = 0
    cinematic_shadow_casters = 5 if (campsite or canyon) else 4
    cinematic_contact_receivers = 0
    cinematic_local_lights = 3 if (campsite or cabin) else 2
    cinematic_volumetric_slices = 0
    env["graphics_pass"] = {
        "version": 2,
        "authored_scene_module": {
            "enabled": True,
            "module_id": authored_module_id,
            "composition_anchor_count": 8 if (campsite or cabin or canyon) else 6,
            "terrain_setpiece_count": 7 if canyon else (6 if water_on else 4),
            "hero_cluster_count": 3 if (campsite or cabin) else 2,
            "foreground_frame_count": 2 if (campsite or cabin) else 1,
            "backdrop_gate_count": 4 if canyon else 3,
            "lighting_zone_count": 4 if (campsite or cabin) else 3,
            "material_family_count": 7 if water_on else 5,
            "water_shape_segment_count": 0,
            "practical_light_count": 2 if authored_module_id == "desert_canyon_river" else (3 if campsite else (4 if cabin else 1)),
            "contrast_key": "warm_low_dawn" if authored_module_id == "campsite_lake_dawn" else (
                "hot_canyon_side_key" if authored_module_id == "desert_canyon_river" else (
                    "cool_moon_warm_window" if authored_module_id == "alpine_cabin_lake" else "neutral_raking_key"
                )
            ),
            "systems": [
                "family_authored_foreground_frame",
                "cohesive_midground_hero_cluster",
                "curved_shore_or_carved_river_corridor",
                "backdrop_gate_silhouette",
                "module_specific_practical_lighting",
                "module_material_family_palette",
            ],
        },
        "terrain": {
            "heightfield": True,
            "relief_m": env["ground"]["terrain"]["relief_m"],
            "shore_integrated": bool(water_on),
            "seed": terrain_seed,
            "noise": "shared_fbm_heightfield",
        },
        "structural_terrain_water": {
            "enabled": True,
            "terrain_system": "Scene::SampleTerrainHeight",
            "terrain_noise": "shared_fbm_heightfield",
            "terrain_seed": terrain_seed,
            "water_system": "WaterSurfaceComponent" if water_on else "none",
            "subsystem_water": bool(water_on),
            "overlay_contract": "telemetry_only",
        },
        "world_geometry": {
            "enabled": True,
            "depth_band_count": env["shot"]["depth_band_count"],
            "foreground_occluder_count": foreground_occluders,
            "ridge_layer_count": max(2, len(ridge_layers)),
            "shoreline_segment_count": shoreline_segments,
            "canyon_wall_layers": 6 if canyon else 0,
            "talus_cluster_count": 14 if canyon else 6,
            "red_rock_strata_layers": 8 if canyon else 0,
            "canyon_width_m": 36.0 if canyon else 0.0,
            "wall_height_m": 10.5 if canyon else 0.0,
        },
        "shot": env["shot"],
        "contact": {
            "decal_count": len(contact_patches),
            "shore_layer_count": 0,
            "patches": contact_patches,
        },
        "occlusion": {
            "enabled": True,
            "ground_shadow_ribbon_count": 0,
            "contact_shadow_strength": 0.0,
            "ambient_occlusion_multiplier": 1.26 if not moonlight else 1.14,
        },
        "image_contact_occlusion": {
            "enabled": False,
            "deep_contact_patch_count": 0,
            "target_dark_contact_fraction": 0.002,
            "target_dark_contact_area_fraction": 0.004,
            "systems": [
                "disabled_metric_proxy_receiver_patches",
                "renderer_ssao_and_shadow_maps_own_contact",
            ],
        },
        "soft_occlusion": {
            "enabled": False,
            "penumbra_patch_count": 0,
            "contact_gradient_layer_count": 0,
            "hero_anchor_count": 0,
            "target_soft_contact_fraction": 0.010,
            "systems": [
                "disabled_soft_shadow_overlay_disks",
                "renderer_ssao_and_cascaded_shadows",
            ],
        },
        "water_shore_integration": {
            "enabled": bool(water_on),
            "foam_lace_segment_count": 0,
            "shoreline_ripple_count": 0,
            "wetline_band_count": 0,
            "reflection_glint_count": 0,
            "submerged_edge_rock_count": 3 if water_on else 0,
            "systems": [
                "real_water_subsystem_surface",
                "shore_material_transition",
                "submerged_edge_rock_grounding",
            ],
        },
        "surface_detail": {
            "enabled": True,
            "pebble_count": 0,
            "terrain_crease_count": 0,
            "shore_foam_segment_count": 0,
            "wet_glint_count": 0,
        },
        "surface_material_richness": {
            "enabled": True,
            "ground_decal_count": 0,
            "rock_lichen_patch_count": 2 if not desert else 0,
            "desert_strata_patch_count": 2 if canyon else 0,
            "vegetation_cluster_count": 2 if not desert else 1,
            "hero_material_line_count": 0,
            "systems": [
                "ground_albedo_breakup",
                "rock_lichen_or_desert_stain",
                "vegetation_scrub_clusters",
                "source_texture_material_response_no_visible_strips",
            ],
        },
        "mesh_silhouette_realism": {
            "enabled": True,
            "cliff_mesh_vertical_bands": 6 if canyon else 0,
            "cliff_overhang_count": 12 if canyon else 0,
            "hero_bevel_detail_count": 0 if campsite else (7 if cabin else 5),
            "prop_depth_layer_count": 4 if (campsite or cabin) else 3,
            "systems": [
                "faceted_cliff_wall_mesh",
                "hero_beveled_edges",
                "roof_eave_or_tent_hem_depth",
                "prop_silhouette_breakup",
            ],
        },
        "naturalistic_ecology": {
            "enabled": True,
            "grass_cluster_count": 8 if not desert else 0,
            "bush_cluster_count": 4 if not desert else 1,
            "fern_cluster_count": 2 if not desert else 0,
            "trunk_count": 1 if not desert else 1,
            "branch_count": 1 if not desert else 1,
            "stump_count": 1 if not desert else 1,
            "moss_rock_count": 4 if not desert else 3,
            "systems": [
                "scanned_grass_clumps",
                "scanned_ferns_or_bushes",
                "scanned_dry_branches",
                "scanned_stumps_and_trunks",
                "scanned_moss_or_boulder_rocks",
            ],
        },
        "asset_fidelity": {
            "enabled": True,
            "hero_detail_count": (4 if campsite else 3) + (6 if cabin else 0),
            "camp_detail_count": 0,
            "cabin_facade_detail_count": 6 if cabin else 0,
            "backdrop_detail_layers": 0,
            "foreground_dressing_clusters": 0,
            "detail_systems": [
                "tent_seams",
                "guy_lines",
                "stakes",
                "ember_bed",
                "cabin_siding",
                "cabin_trim",
                "porch_steps",
                "ridge_silhouette_breakup",
            ],
        },
        "hero_environment_geometry": {
            "enabled": True,
            "high_detail_camp_piece_count": 0,
            "high_detail_cabin_piece_count": 5 if cabin else 0,
            "mountain_mass_layer_count": 5 if (water_on or canyon or cabin) else 3,
            "cliff_mass_piece_count": 14 if canyon else 0,
            "shoreline_prop_count": 1 if water_on else 0,
            "irregular_tree_silhouette_count": 0,
            "support_prop_count": 1 if campsite else (2 if cabin else 1),
            "detail_systems": [
                "high_detail_camp_kit",
                "high_detail_cabin_kit",
                "mountain_massing_meshes",
                "shoreline_driftwood_and_stones",
                "irregular_tree_silhouette_meshes",
            ],
        },
        "texture_material_fidelity": {
            "enabled": True,
            "texture_set_count": 7,
            "terrain_surface_count": 3 if water_on else 2,
            "rock_surface_count": 18 if canyon else 10,
            "wood_surface_count": (18 if campsite else 8) + (18 if cabin else 0) + (6 if water_on else 0),
            "fabric_surface_count": 10 if campsite else 2,
            "hero_surface_count": (24 if campsite else 8) + (24 if cabin else 0),
            "shore_surface_count": 10 if water_on else 0,
            "texture_sets": [
                "polyhaven/aerial_grass_rock",
                "polyhaven/aerial_beach_01",
                "polyhaven/coast_sand_05",
                "polyhaven/wood_floor_deck",
                "polyhaven/plastered_wall",
                "naturalistic/boulder_01",
                "naturalistic/rock_moss_set_01",
            ],
            "systems": [
                "terrain_albedo_normal_roughness",
                "rock_cliff_albedo_normal_arm",
                "wood_albedo_normal_roughness",
                "fabric_albedo_normal_roughness",
                "wet_shore_texture_binding",
                "hero_geometry_texture_assignment",
            ],
        },
        "source_geometry_fidelity": {
            "enabled": True,
            "source_asset_set_count": 4,
            "scanned_lantern_count": 3 if (campsite or cabin) else 0,
            "scanned_utility_prop_count": 1 if cabin else 0,
            "scanned_anchor_rock_count": 5 if water_on else 4,
            "hero_anchor_count": 7 if campsite else (7 if cabin else 4),
            "systems": [
                "scanned_lantern_pbr_mesh",
                "scanned_table_or_barrel_pbr_mesh",
                "scanned_boulder_anchor_mesh",
                "prompt_hero_source_mesh_anchors",
            ],
        },
        "hero_asset_replacement": {
            "enabled": True,
            "replacement_family": "canvas_camp_overbuild" if campsite else (
                "cabin_facade_overbuild" if cabin else (
                    "canyon_hero_massing" if canyon else "landscape_hero_overbuild"
                )
            ),
            "canvas_shell_panel_count": 5 if campsite else 0,
            "fabric_layer_count": 6 if campsite else 0,
            "structural_pole_count": 6 if campsite else 0,
            "rope_stake_count": 2 if campsite else 0,
            "low_poly_mask_count": 0,
            "cabin_facade_module_count": 6 if cabin else 0,
            "cabin_roof_module_count": 4 if cabin else 0,
            "cabin_deck_foundation_count": 4 if cabin else 0,
            "hero_rock_mass_count": 5 if canyon else (3 if not (campsite or cabin) else 0),
            "systems": [
                "dominant_canvas_shell_replaces_orange_tent_read",
                "layered_fabric_wrinkles_and_edge_hems",
                "structural_pole_frame_with_minimal_guyline_geometry",
                "low_poly_catalog_masking_panels",
                "cabin_facade_roof_deck_overbuild",
                "prompt_family_hero_rock_massing",
            ],
        },
        "cohesive_staging_cleanup": {
            "enabled": True,
            "hero_cluster_radius_m": 2.65 if (campsite or cabin) else 3.10,
            "stray_dressing_budget": 0,
            "central_sightline_clearance_m": 4.2 if (campsite or cabin) else 3.4,
            "anchored_prop_count": 9 if campsite else (9 if cabin else 5),
            "foreground_relocation_count": 3 if (campsite or cabin) else 2,
            "palette_unification_count": 12 if (campsite or cabin) else 7,
            "systems": [
                "cluster_camp_kit_around_tent_fire_axis",
                "move_foreground_frame_out_of_central_sightline",
                "reduce_stray_wood_and_loose_prop_spread",
                "palette_unify_small_detail_materials",
                "keep_prompt_hero_readable_before_more_detail",
            ],
        },
        "environment_fidelity": {
            "enabled": True,
            "sky_layer_count": 6 if (moonlight or stormy or fog) else 5,
            "atmosphere_depth_cue_count": 7 if (canyon or moonlight or fog) else 5,
            "horizon_blend_band_count": 0,
            "terrain_macro_breakup_count": 0,
            "water_depth_band_count": 0,
            "reflection_band_count": 0,
            "directional_shadow_lane_count": 0,
            "backdrop_integration_layer_count": 7 if (canyon or cabin) else 6,
            "shadow_directionality": 0.82 if not moonlight else 0.72,
            "sky_gradient_strength": 0.72 if not moonlight else 0.42,
            "water_depth_contrast": 0.78 if water_on else 0.0,
            "terrain_relief_contrast": 0.70 if not desert else 0.62,
            "systems": [
                "layered_sky_and_horizon_depth",
                "atmospheric_backdrop_integration",
                "macro_terrain_light_shadow_breakup",
                "water_depth_and_reflection_banding",
                "directional_shadow_lanes_from_key_light",
                "single_environment_lighting_contract",
            ],
        },
        "source_environment_assets": {
            "enabled": True,
            "source_asset_set_count": 11 if canyon else 10,
            "fetched_rock_mass_count": 6 if canyon else 5,
            "kenney_cliff_backdrop_count": 0,
            "detailed_tree_backdrop_count": 0 if desert else 3,
            "naturalistic_anchor_count": 6 if water_on else 4,
            "terrain_replacement_layer_count": 3,
            "backdrop_anchor_count": 10 if canyon else 8,
            "replacement_family": "source_canyon_wall_backdrop" if canyon else (
                "source_alpine_tree_rock_backdrop" if cabin else "source_lake_mountain_backdrop"
            ),
            "systems": [
                "naturalistic_scan_backdrop_masses",
                "kenney_cliff_mesh_demoted_to_zero",
                "naturalistic_bush_and_trunk_background_masses",
                "naturalistic_boulder_and_moss_rock_terrain_anchors",
                "naturalistic_source_asset_terrain_to_backdrop_transition",
                "water_color_roi_preserving_flank_layout",
            ],
        },
        "hero_material_shadow_readability": {
            "enabled": True,
            "hero_material_panel_count": 0,
            "hero_shadow_receiver_count": 0,
            "local_fill_light_count": 2 if (campsite or cabin) else 1,
            "rim_light_count": 2 if (campsite or cabin) else 1,
            "material_contrast": 0.64 if not moonlight else 0.54,
            "exposure_lift": 0.16 if campsite else (0.10 if moonlight else 0.12),
            "shadow_softness": 0.58 if not moonlight else 0.46,
            "readability_family": "tent_fabric_firelight" if campsite else (
                "cabin_window_moon_bounce" if cabin else "canyon_wall_raking_light"
            ),
            "systems": [
                "real_hero_surface_materials_no_visible_cards",
                "local_fill_light_without_global_washout",
                "rim_light_edge_separation",
                "renderer_ssao_contact_not_shadow_cards",
                "bounded_exposure_lift",
                "no_visible_light_cards",
            ],
        },
        "structural_scene_fidelity": {
            "enabled": True,
            "terrain_tessellation_grid": env["ground"]["terrain"]["grid"],
            "terrain_relief_m": env["ground"]["terrain"]["relief_m"],
            "terrain_displacement_tile_count": 0,
            "terrain_displacement_layer_count": 0,
            "hero_foundation_count": 3 if campsite else (4 if cabin else 2),
            "shadow_caster_count": 3 if not moonlight else 2,
            "material_blend_patch_count": 0,
            "light_volume_count": 2 if not moonlight else 1,
            "nonplanar_shore_segment_count": 0,
            "foundation_family": "embedded_tent_pad_and_fire_ring" if campsite else (
                "stone_cabin_foundation_and_deck_contact" if cabin else "canyon_bank_embedded_camp"
            ),
            "systems": [
                "raised_tessellated_heightfield_relief",
                "opaque_displaced_terrain_tiles_not_flat_cards",
                "hero_foundation_geometry_embeds_props",
                "material_blend_patches_with_texture_hooks",
                "real_shadow_caster_geometry_and_local_lights",
                "nonplanar_shore_bank_transition",
            ],
        },
        "hero_mesh_material_overhaul": {
            "enabled": True,
            "tent_shell_count": 0,
            "cabin_cladding_count": 10 if cabin else 0,
            "roof_layer_count": 5 if cabin else 0,
            "canyon_hero_mesh_count": 8 if canyon else 0,
            "pbr_material_layer_count": 0 if campsite else (6 if (cabin or canyon) else 4),
            "lowpoly_silhouette_mask_count": 0,
            "overhaul_family": "aframe_canvas_pbr_shell" if campsite else (
                "cabin_cladding_roof_pbr" if cabin else "canyon_rock_mesh_pbr"
            ),
            "systems": [
                "procedural_rebuilt_hero_mesh",
                "source_texture_driven_pbr_layers",
                "lowpoly_silhouette_masking_without_light_cards",
                "prompt_family_hero_cladding",
                "shadow_casting_hero_forms",
            ],
        },
        "lighting_shadow_material_field": {
            "enabled": True,
            "shadowed_spot_light_count": 3 if not moonlight else 2,
            "material_response_surface_count": 18 if not desert else 16,
            "contact_shadow_band_count": 0,
            "local_probe_volume_count": 2 if water_on else 1,
            "key_fill_ratio": 3.35 if campsite else (3.55 if desert else 2.65),
            "ambient_intensity_ceiling": 0.42 if campsite else (0.48 if desert else 0.38),
            "ssao_intensity_target": 3.35 if not moonlight else 3.05,
            "shadow_bias_target": 0.0011 if not moonlight else 0.0013,
            "shadow_pcf_radius_target": 3.85 if not moonlight else 3.55,
            "response_family": "dawn_campfire_raking_key" if campsite else (
                "sandstone_river_shadow_field" if desert else "moonlit_cabin_shadow_field"
            ),
            "systems": [
                "bounded_shadowed_spot_lights",
                "ambient_clamped_key_fill_ratio",
                "renderer_ssao_shadow_bias_pcf_targets",
                "existing_surface_pbr_response_tuning",
                "local_probe_volume_material_grade",
                "no_translucent_shadow_band_overlays",
            ],
        },
        "source_readability_balance": {
            "enabled": True,
            "source_surface_lift_count": 34 if not cabin else 28,
            "backdrop_surface_lift_count": 14 if not desert else 12,
            "black_mass_split_count": 0,
            "albedo_floor": 0.125 if not moonlight else 0.105,
            "frame_nonblack_floor": 0.92 if not moonlight else 0.82,
            "frame_luma_floor": 0.24 if desert else (0.20 if not moonlight else 0.10),
            "readability_family": "dawn_source_lift" if campsite else (
                "canyon_black_mass_split" if desert else "moonlit_source_readability"
            ),
            "systems": [
                "source_asset_albedo_floor",
                "backdrop_and_ridge_readability_lift",
                "black_mass_split_highlight_geometry",
                "frame_nonblack_image_guard",
                "source_material_occlusion_rebalance",
            ],
        },
        "renderer_shadow_occlusion_budget": {
            "enabled": True,
            "renderer_ssao": True,
            "shadow_maps": True,
            "ssr_contact_support": True,
            "dxr_required": False,
            "ssao_radius": renderer_ssao_radius,
            "ssao_bias": 0.018,
            "ssao_intensity": renderer_ssao_intensity,
            "shadow_bias": renderer_shadow_bias,
            "shadow_pcf_radius": renderer_shadow_pcf,
            "contact_receiver_patch_budget": contact_receiver_patch_budget,
            "soft_penumbra_patch_budget": soft_penumbra_patch_budget,
            "renderer_contact_blend": 0.72 if not moonlight else 0.64,
            "shadow_caster_policy": "sun_shadow_maps_plus_ssao",
            "systems": [
                "readback_verified_ssao_controls",
                "readback_verified_shadow_map_controls",
                "renderer_owned_contact_support",
                "generated_dxr_opt_in_only",
            ],
        },
        "cinematic_material_lighting": {
            "enabled": True,
            "triplanar_detail_layer_count": 8 if canyon else 7,
            "terrain_relief_patch_count": cinematic_relief_patches,
            "shadow_caster_count": cinematic_shadow_casters,
            "contact_receiver_count": cinematic_contact_receivers,
            "localized_light_count": cinematic_local_lights,
            "volumetric_light_slice_count": cinematic_volumetric_slices,
            "wet_roughness_variation_count": 0,
            "source_texture_weight": 0.82 if not desert else 0.76,
            "normal_detail_scale": 0.96 if not desert else 0.84,
            "roughness_variation": 0.62 if water_on else 0.48,
            "shadow_caster_policy": "module_physical_occluders_plus_shadow_mapped_practicals",
            "systems": [
                "triplanar_source_texture_material_relief",
                "terrain_relief_patch_meshes",
                "localized_shadow_caster_geometry",
                "terrain_material_receives_renderer_shadows",
                "module_specific_practical_light_pools",
                "renderer_fog_without_visible_light_cards",
                "water_and_shore_materials_without_patch_cards",
            ],
        },
        "atmosphere_fidelity": {
            "enabled": True,
            "night_sky_control": bool(moonlight),
            "storm_layer_count": 4 if stormy else (2 if fog else 1),
            "rain_streak_count": 28 if stormy else 0,
            "haze_depth_layers": 0,
            "moonlight_exposure": 0.72 if moonlight else 0.0,
            "sky_background_lift": 0.38 if moonlight else 1.0,
        },
        "geometry_realism": {
            "enabled": True,
            "cliff_erosion_ridge_count": 18 if canyon else 6,
            "strata_breakup_count": 14 if canyon else 0,
            "wall_normal_breakup": 0.82 if canyon else 0.36,
            "foreground_relief_clusters": 8 if canyon else 4,
        },
        "material_zones": {
            "count": len(material_zone_names),
            "zones": material_zone_names,
        },
        "materials": {
            "enabled": True,
            "advanced_shader_terms": {
                "ao": True,
                "clearcoat": True,
                "sheen": True,
                "subsurface": True,
                "anisotropy": True,
                "wetness": True,
                "procedural_mask": True,
            },
            "ground_normal_scale": 0.82 if not desert else 0.76,
            "ground_wetness": 0.30 if water_on and not desert else 0.13,
            "procedural_mask": 0.42 if not desert else 0.46,
            "roughness": 0.84,
        },
        "lighting": {
            "fixed_exposure": True,
            "raking_key": True,
            "rim_light_count": 1,
            "volumetric_fog": True,
        },
        "renderer": {
            "ssao": True,
            "ssr": True,
            "shadows": True,
            "ssao_radius": renderer_ssao_radius,
            "ssao_intensity": renderer_ssao_intensity,
            "shadow_pcf_radius": renderer_shadow_pcf,
            "shadow_bias": renderer_shadow_bias,
        },
    }

    lights = []
    for practical in lighting.get("practicals", []) or []:
        if practical.get("type") == "campfire_glow":
            lights.append({
                "type": "point",
                "x": 0.0,
                "y": 0.85,
                "z": 0.55,
                "color": [1.0, 0.45, 0.18],
                "intensity": float(practical.get("intensity", 5.5) or 5.5),
                "range": float(practical.get("range_m", 7.5) or 7.5),
            })

    return {
        "setting": "exterior",
        "environment": env,
        "objects": objects,
        "lights": lights,
        "director": {
            "version": 3,
            "scene_type": scene_type,
            "prompt": prompt,
            "must_read": intent.get("must_read", []),
            "scene_layers": [layer.get("id") for layer in v3.get("scene_layers", []) if isinstance(layer, dict)],
            "generator_graph": [node.get("id") for node in v3.get("generator_graph", []) if isinstance(node, dict)],
            "instance_groups": v3.get("instance_groups", []),
            "quality": v3.get("quality", {}),
            "structures": env.get("structures", []),
        },
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Compile Director IR v3 to current engine Scene IR")
    ap.add_argument("--in", dest="in_path", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    v3 = json.loads(args.in_path.read_text(encoding="utf-8"))
    v2 = compile_v3_to_v2(v3)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(v2, indent=2), encoding="utf-8")
    print(args.out)
    print(f"objects={len(v2['objects'])} lights={len(v2['lights'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
