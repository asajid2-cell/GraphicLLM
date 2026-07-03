#!/usr/bin/env python3
"""Director IR v3 schema helpers.

Director IR is the scene source of truth. It keeps art-direction layers,
spatial masks, generator graph passes, asset prototypes, instance groups,
lighting/look, budgets, and quality constraints. A separate compiler lowers it
to the current engine Scene IR until the C++ runtime grows native v3 fields.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


VERSION = 3

REQUIRED_TOP_LEVEL = {
    "version",
    "intent",
    "scene_layers",
    "spatial_regions",
    "generator_graph",
    "scale_grids",
    "world",
    "asset_prototypes",
    "instance_groups",
    "lighting_look",
    "materials",
    "quality",
}

REQUIRED_INTENT = {"prompt", "scene_class", "scene_type", "must_read", "reject_if"}
REQUIRED_WORLD = {"terrain", "waterbody", "background", "scatter_masks"}
REQUIRED_LIGHTING = {"time", "sun", "atmosphere", "post"}


COLOR_WORDS = {
    "purple": ([0.45, 0.28, 0.78], [0.08, 0.03, 0.20]),
    "violet": ([0.48, 0.30, 0.82], [0.09, 0.03, 0.22]),
    "blue": ([0.20, 0.42, 0.82], [0.02, 0.08, 0.28]),
    "turquoise": ([0.05, 0.70, 0.72], [0.00, 0.20, 0.26]),
    "green": ([0.10, 0.52, 0.36], [0.01, 0.23, 0.19]),
}


def _slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")[:48] or "scene"


def water_palette(prompt: str) -> tuple[str, list[float], list[float]]:
    p = prompt.lower()
    for name, (shallow, deep) in COLOR_WORDS.items():
        if f"{name} lake" in p or f"{name} water" in p or f"{name} river" in p:
            return name, shallow, deep
    if "river" in p:
        return "turquoise", COLOR_WORDS["turquoise"][0], COLOR_WORDS["turquoise"][1]
    return "natural_green", COLOR_WORDS["green"][0], COLOR_WORDS["green"][1]


def scene_type_for(prompt: str) -> str:
    p = prompt.lower()
    if "canyon" in p and ("river" in p or "water" in p):
        return "desert_canyon_river"
    if "cabin" in p and ("lake" in p or "alpine" in p):
        return "alpine_lake_cabin"
    if "camp" in p and ("lake" in p or "river" in p or "mountain" in p):
        return "mountain_lake_campsite"
    if "beach" in p:
        return "beach"
    if "forest" in p:
        return "forest"
    return "exterior_landscape"


def director_from_prompt(prompt: str) -> dict[str, Any]:
    scene_type = scene_type_for(prompt)
    color_name, shallow, deep = water_palette(prompt)
    p = prompt.lower()
    dawn = any(w in p for w in ("dawn", "sunrise", "daybreak"))
    stormy = "storm" in p
    moon = "moon" in p or "moonlight" in p
    desert = "desert" in p or "canyon" in p
    campsite = "camp" in p
    cabin = "cabin" in p
    water_kind = "river" if "river" in p else "lake" if "lake" in p else "waterbody"
    terrain_base = "red_rock_sand" if desert else "grass_rock_moss"
    must_read = []
    for flag, words in [
        ("campsite", ("camp", "campsite")),
        ("cabin", ("cabin",)),
        ("purple_lake", ("purple lake", "purple water")),
        ("turquoise_river", ("turquoise river", "turquoise water")),
        ("mountains", ("mountain", "alpine", "ridge", "canyon")),
        ("dawn", ("dawn", "sunrise", "daybreak")),
        ("fog", ("fog", "foggy", "mist", "misty", "haze")),
        ("storm", ("storm", "stormy")),
        ("moonlight", ("moon", "moonlight")),
    ]:
        if any(w in p for w in words):
            must_read.append(flag)
    if water_kind != "waterbody":
        must_read.append(water_kind)

    focal = "tent_and_campfire" if campsite else "cabin" if cabin else "landscape_hero"
    setpiece_prototypes = [
        {"id": "shore_rock", "query": "mossy shoreline rock" if not desert else "red canyon rock", "role": "rock", "quality": "scatter"},
        {"id": "pine_tree", "query": "pine tree" if not desert else "desert shrub", "role": "tree" if not desert else "bush", "quality": "scatter"},
        {"id": "grass_tuft", "query": "grass tuft" if not desert else "dry grass tuft", "role": "grass", "quality": "scatter"},
    ]
    if campsite:
        setpiece_prototypes += [
            {"id": "hero_tent", "query": "open campsite tent", "role": "camp", "quality": "hero"},
            {"id": "campfire", "query": "campfire", "role": "camp", "quality": "hero_light"},
            {"id": "log_seat", "query": "fallen log", "role": "tree", "quality": "hero"},
        ]
    if cabin:
        setpiece_prototypes.append({"id": "hero_cabin", "query": "small rustic cabin", "role": "structure", "quality": "hero"})

    ir = {
        "version": VERSION,
        "intent": {
            "prompt": prompt,
            "scene_class": "exterior",
            "scene_type": scene_type,
            "must_read": sorted(set(must_read)),
            "style": ["cinematic", "naturalistic", "dense"],
            "reject_if": ["interior_assets", "flat_empty_plane", "gray_water", "missing_horizon"],
        },
        "scene_layers": [
            {"id": "terrain", "kind": "heightfield", "priority": 10},
            {"id": "waterbody", "kind": water_kind, "priority": 20},
            {"id": "background_ridges", "kind": "backdrop", "priority": 30},
            {"id": "hero_set", "kind": "shot_grammar", "priority": 40},
            {"id": "scatter_detail", "kind": "pcg_scatter", "priority": 50},
            {"id": "lighting_look", "kind": "render_state", "priority": 60},
        ],
        "spatial_regions": {
            "foreground_frame": {"z": [2.5, 7.0], "x": [-16.0, 16.0]},
            "hero_midground": {"z": [-0.5, 3.5], "x": [-7.0, 7.0]},
            "water_band": {"z": [-18.0, -5.0], "x": [-22.0, 22.0]},
            "ridge_horizon": {"z": [-58.0, -34.0], "x": [-34.0, 34.0]},
            "tree_flanks": {"mask": "left_right_background"},
            "clear_view_corridor": {"x": [-3.0, 3.0], "z": [-2.5, -28.0]},
        },
        "generator_graph": [
            {"id": "terrain_massing", "kind": "heightfield", "inputs": ["world.terrain"], "outputs": ["terrain_height", "shore_mask"]},
            {"id": "ridge_backdrop", "kind": "procedural_mesh", "inputs": ["world.background"], "outputs": ["ridge_meshes"]},
            {"id": "waterbody_shape", "kind": water_kind, "inputs": ["shore_mask"], "outputs": ["water_mesh", "wet_shore_mask"]},
            {"id": "hero_grammar", "kind": "shape_grammar", "inputs": ["asset_prototypes"], "outputs": ["hero_instances"]},
            {"id": "scatter_points", "kind": "scatter", "inputs": ["scatter_masks"], "outputs": ["tree_instances", "rock_instances", "grass_instances"]},
        ],
        "scale_grids": {
            "landmarks": {"cell_m": 32, "budget": 8},
            "mid_assets": {"cell_m": 8, "budget": 36},
            "ground_detail": {"cell_m": 2, "budget": 120},
        },
        "world": {
            "terrain": {
                "extent_m": 48,
                "base": terrain_base,
                "heightfield": {"enabled": True, "undulation": 0.45 if not desert else 0.75},
                "shoreline": {"shape": "curved_cove" if water_kind == "lake" else "s_curve", "wet_edge_m": 2.0},
            },
            "waterbody": {
                "type": water_kind,
                "bounds": "far_mid_band",
                "color_intent": color_name,
                "shallow": shallow,
                "deep": deep,
                "roughness": 0.16 if stormy else 0.10,
                "reflection_weight": 0.32,
            },
            "background": {
                "ridge_layers": [
                    {"distance_m": 55, "height_m": 10 if not desert else 13, "color": [0.18, 0.20, 0.25]},
                    {"distance_m": 84, "height_m": 18 if not desert else 22, "color": [0.10, 0.12, 0.18]},
                ],
            },
            "scatter_masks": {
                "tree_flanks": "left_right_background",
                "rocks": "shore_and_foreground",
                "grass": "near_camera_breakup",
                "clear": "camera_to_focal_view_corridor",
            },
        },
        "asset_prototypes": setpiece_prototypes,
        "instance_groups": [
            {"prototype": "pine_tree", "source": "tree_flanks", "budget": {"min": 18, "max": 72}},
            {"prototype": "shore_rock", "source": "shore_mask", "budget": {"min": 14, "max": 48}},
            {"prototype": "grass_tuft", "source": "foreground_frame", "budget": {"min": 24, "max": 96}},
        ],
        "setpieces": [
            {"prototype": "hero_tent" if campsite else "hero_cabin" if cabin else "shore_rock", "role": "hero", "placement": "hero_midground_right", "scale_m": 2.8},
            {"prototype": "campfire", "role": "hero_light", "placement": "hero_midground_center", "scale_m": 1.0} if campsite else {"prototype": "shore_rock", "role": "foreground_anchor", "placement": "foreground_frame", "scale_m": 2.0},
            {"prototype": "log_seat", "role": "leading_lines", "placement": "around_fire", "scale_m": 1.4} if campsite else {"prototype": "pine_tree", "role": "flank", "placement": "tree_flanks", "scale_m": 5.0},
        ],
        "lighting_look": {
            "time": "moonlight" if moon else "dawn" if dawn else "storm" if stormy else "day",
            "sun": {
                "elevation_deg": 8 if dawn else 18 if stormy else 45,
                "azimuth_deg": 135,
                "color": [1.0, 0.55, 0.28] if dawn else [0.55, 0.62, 0.90] if moon else [1.0, 0.92, 0.80],
                "intensity": 3.2 if dawn else 1.2 if moon or stormy else 3.4,
            },
            "fill": {"sky_ibl": "cool_low" if dawn or moon or stormy else "day", "strength": 0.75},
            "practicals": [{"type": "campfire_glow", "intensity": 5.5, "range_m": 7.5}] if campsite else [],
            "atmosphere": {
                "fog": {
                    "density": 0.018 if any(w in p for w in ("fog", "mist", "haze")) else 0.009,
                    "start_m": 5.0,
                    "height_falloff": 0.22,
                },
                "particles": {"mist": 0.8 if "fog" in p or "mist" in p else 0.25, "embers": 0.3 if campsite else 0.0},
            },
            "post": {
                "exposure": 0.9 if dawn or stormy else 1.05,
                "contrast": 1.12,
                "saturation": 1.08,
                "bloom": 0.35 if campsite or moon else 0.20,
                "vignette": 0.18,
            },
        },
        "materials": {
            "water": {"absorption": 0.85, "fresnel": 0.28, "foam": 0.20 if water_kind == "lake" else 0.38},
            "shore": {"wetness": 0.55, "roughness": 0.38},
            "ground": {"base": terrain_base, "moss_bias": 0.7 if not desert else 0.05},
            "rocks": {"roughness": 0.82, "moss": 0.35 if not desert else 0.0},
        },
        "quality": {
            "min_instances": 80,
            "budgets": {"hero_assets": 3, "scatter_instances": 80, "max_unique_meshes": 24},
            "must_have_pixel_color": {"region": "waterbody", "hue": color_name},
            "known_bad_rejects": ["kitchenfridge", "flat_gray_water", "empty_horizon"],
        },
        "shot": {
            "composition": "foreground_frame_midground_hero_background_horizon",
            "focal_subject": focal,
            "camera": {"height_m": 1.45, "fov_deg": 55, "lens": "wide_cinematic"},
            "bands": ["foreground_frame", "hero_midground", "water_band", "ridge_horizon"],
        },
    }
    return ir


def example_campsite() -> dict[str, Any]:
    return director_from_prompt("a foggy mountain campsite beside a purple lake at dawn")


def validate(ir: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if ir.get("version") != VERSION:
        errors.append(f"version must be {VERSION}")
    missing = sorted(REQUIRED_TOP_LEVEL - set(ir))
    if missing:
        errors.append(f"missing top-level keys: {missing}")
    intent = ir.get("intent")
    if not isinstance(intent, dict):
        errors.append("intent must be an object")
    else:
        m = sorted(REQUIRED_INTENT - set(intent))
        if m:
            errors.append(f"intent missing keys: {m}")
        if not intent.get("must_read"):
            errors.append("intent.must_read must be non-empty")
    world = ir.get("world")
    if not isinstance(world, dict):
        errors.append("world must be an object")
    else:
        m = sorted(REQUIRED_WORLD - set(world))
        if m:
            errors.append(f"world missing keys: {m}")
        bg = (world.get("background") or {}) if isinstance(world.get("background"), dict) else {}
        if not bg.get("ridge_layers"):
            errors.append("world.background.ridge_layers must be non-empty")
    lighting = ir.get("lighting_look")
    if not isinstance(lighting, dict):
        errors.append("lighting_look must be an object")
    else:
        m = sorted(REQUIRED_LIGHTING - set(lighting))
        if m:
            errors.append(f"lighting_look missing keys: {m}")
    for key in ("scene_layers", "generator_graph", "asset_prototypes", "instance_groups"):
        if not isinstance(ir.get(key), list) or not ir.get(key):
            errors.append(f"{key} must be a non-empty list")
    regions = ir.get("spatial_regions")
    if not isinstance(regions, dict) or not regions:
        errors.append("spatial_regions must be a non-empty object")
    quality = ir.get("quality")
    if not isinstance(quality, dict):
        errors.append("quality must be an object")
    else:
        if int(quality.get("min_instances", 0) or 0) < 40:
            errors.append("quality.min_instances must be at least 40")
        if not quality.get("must_have_pixel_color"):
            errors.append("quality.must_have_pixel_color required")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser(description="Director IR v3 schema tool")
    ap.add_argument("--example", choices=["campsite"], help="write an example Director IR")
    ap.add_argument("--prompt", help="generate Director IR heuristically from prompt")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--validate", type=Path, help="validate a Director IR JSON file")
    args = ap.parse_args()

    if args.validate:
        ir = json.loads(args.validate.read_text(encoding="utf-8"))
        errors = validate(ir)
        if errors:
            print(json.dumps({"valid": False, "errors": errors}, indent=2))
            return 1
        print(json.dumps({"valid": True, "scene_type": ir["intent"]["scene_type"]}, indent=2))
        return 0

    if args.example:
        ir = example_campsite()
    elif args.prompt:
        ir = director_from_prompt(args.prompt)
    else:
        ap.error("provide --example, --prompt, or --validate")

    errors = validate(ir)
    if errors:
        print(json.dumps({"valid": False, "errors": errors}, indent=2), file=sys.stderr)
        return 1
    text = json.dumps(ir, indent=2)
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
        print(args.out)
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
