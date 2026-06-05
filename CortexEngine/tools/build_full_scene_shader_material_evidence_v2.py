#!/usr/bin/env python3
"""Build Full Scene Shader Pipeline V2 material evidence.

This derives a shader-facing material-readiness report from Asset Registry V2
and the scene-object binding overlay. It does not mark assets AAA-ready; it
makes the missing evidence explicit so renderer V2 can fail honestly instead
of guessing that a mesh is game-ready.
"""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REGISTRY = ROOT / "assets/final_art/asset_registry_v2.json"
DEFAULT_BINDINGS = ROOT / "assets/final_art/scene_asset_bindings_v1.json"
DEFAULT_OUT = ROOT / "assets/final_art/full_scene_shader_material_evidence_v2.json"
DEFAULT_MD = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_evidence_report.md"
)


MATERIAL_FAMILY_TOKENS: list[tuple[str, str]] = [
    ("emissive", "emissive"),
    ("neon", "emissive"),
    ("screen", "screen"),
    ("display", "screen"),
    ("mirror", "mirror"),
    ("glass", "glass"),
    ("water", "water"),
    ("wet", "water"),
    ("brushed", "brushed_metal"),
    ("metal", "metal"),
    ("steel", "metal"),
    ("chrome", "metal"),
    ("wood", "wood"),
    ("cabinet", "wood"),
    ("fabric", "fabric"),
    ("seat", "fabric"),
    ("tile", "tile"),
    ("ceramic", "ceramic"),
    ("wall", "painted_wall"),
    ("concrete", "painted_wall"),
    ("rubber", "rubber"),
    ("plastic", "plastic"),
    ("floor", "tile"),
]

FAMILY_FEATURES: dict[str, set[str]] = {
    "dielectric": {"base_color_texture", "normal_texture", "orm_texture"},
    "metal": {"base_color_texture", "normal_texture", "orm_texture"},
    "brushed_metal": {
        "base_color_texture",
        "normal_texture",
        "orm_texture",
        "anisotropy",
    },
    "glass": {"base_color_texture", "normal_texture", "orm_texture", "transmission", "opacity"},
    "water": {"normal_texture", "orm_texture", "transmission", "detail_normal"},
    "emissive": {"base_color_texture", "emissive_texture"},
    "wood": {"base_color_texture", "normal_texture", "orm_texture", "detail_normal"},
    "fabric": {"base_color_texture", "normal_texture", "orm_texture", "detail_normal"},
    "ceramic": {"base_color_texture", "normal_texture", "orm_texture", "clearcoat"},
    "tile": {"base_color_texture", "normal_texture", "orm_texture", "detail_normal"},
    "painted_wall": {"base_color_texture", "normal_texture", "orm_texture", "detail_normal"},
    "rubber": {"base_color_texture", "normal_texture", "orm_texture"},
    "plastic": {"base_color_texture", "normal_texture", "orm_texture"},
    "mirror": {"base_color_texture", "normal_texture", "orm_texture", "clearcoat"},
    "screen": {"base_color_texture", "emissive_texture"},
    "unknown": set(),
}

FEATURE_TEXTURE_SLOTS: dict[str, str] = {
    "base_color_texture": "base_color",
    "normal_texture": "normal",
    "orm_texture": "orm_or_separate_occlusion_roughness_metallic",
    "emissive_texture": "emissive",
    "transmission": "opacity_or_transmission",
    "opacity": "opacity_or_transmission",
    "detail_normal": "detail_normal_or_height",
    "parallax": "height_or_parallax",
}

RUNTIME_POLICY_BY_FAMILY: dict[str, dict[str, Any]] = {
    "dielectric": {
        "scene_material_class": "Default",
        "scene_material_class_id": 0,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
    "metal": {
        "scene_material_class": "PolishedMetal",
        "scene_material_class_id": 5,
        "reflection_preference": "RTReflection",
        "reflection_preference_id": 5,
        "temporal_policy": "StableGlossy",
        "temporal_policy_id": 1,
        "post_sensitivity": "ExposureProtected",
        "post_sensitivity_id": 2,
    },
    "brushed_metal": {
        "scene_material_class": "BrushedMetal",
        "scene_material_class_id": 4,
        "reflection_preference": "LocalProbe",
        "reflection_preference_id": 1,
        "temporal_policy": "StableGlossy",
        "temporal_policy_id": 1,
        "post_sensitivity": "ExposureProtected",
        "post_sensitivity_id": 2,
    },
    "glass": {
        "scene_material_class": "GlassPane",
        "scene_material_class_id": 6,
        "reflection_preference": "RTReflection",
        "reflection_preference_id": 5,
        "temporal_policy": "StableGlossy",
        "temporal_policy_id": 1,
        "post_sensitivity": "ExposureProtected",
        "post_sensitivity_id": 2,
    },
    "water": {
        "scene_material_class": "Water",
        "scene_material_class_id": 14,
        "reflection_preference": "PlanarProbe",
        "reflection_preference_id": 3,
        "temporal_policy": "WaterViewDependent",
        "temporal_policy_id": 4,
        "post_sensitivity": "WetHighlight",
        "post_sensitivity_id": 3,
    },
    "emissive": {
        "scene_material_class": "EmissiveNeon",
        "scene_material_class_id": 10,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "EmissiveLocked",
        "temporal_policy_id": 3,
        "post_sensitivity": "BloomEmitter",
        "post_sensitivity_id": 1,
    },
    "wood": {
        "scene_material_class": "PolishedWood",
        "scene_material_class_id": 3,
        "reflection_preference": "LocalProbe",
        "reflection_preference_id": 1,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
    "fabric": {
        "scene_material_class": "Fabric",
        "scene_material_class_id": 7,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
    "ceramic": {
        "scene_material_class": "CeramicTile",
        "scene_material_class_id": 2,
        "reflection_preference": "SSR",
        "reflection_preference_id": 4,
        "temporal_policy": "StableGlossy",
        "temporal_policy_id": 1,
        "post_sensitivity": "ExposureProtected",
        "post_sensitivity_id": 2,
    },
    "tile": {
        "scene_material_class": "CeramicTile",
        "scene_material_class_id": 2,
        "reflection_preference": "SSR",
        "reflection_preference_id": 4,
        "temporal_policy": "StableGlossy",
        "temporal_policy_id": 1,
        "post_sensitivity": "ExposureProtected",
        "post_sensitivity_id": 2,
    },
    "painted_wall": {
        "scene_material_class": "PaintedWall",
        "scene_material_class_id": 1,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
    "rubber": {
        "scene_material_class": "Rubber",
        "scene_material_class_id": 13,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
    "plastic": {
        "scene_material_class": "Plastic",
        "scene_material_class_id": 8,
        "reflection_preference": "LocalProbe",
        "reflection_preference_id": 1,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
    "mirror": {
        "scene_material_class": "Mirror",
        "scene_material_class_id": 15,
        "reflection_preference": "RTReflection",
        "reflection_preference_id": 5,
        "temporal_policy": "MirrorLocked",
        "temporal_policy_id": 2,
        "post_sensitivity": "ExposureProtected",
        "post_sensitivity_id": 2,
    },
    "screen": {
        "scene_material_class": "ScreenPanel",
        "scene_material_class_id": 11,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "EmissiveLocked",
        "temporal_policy_id": 3,
        "post_sensitivity": "BloomEmitter",
        "post_sensitivity_id": 1,
    },
    "unknown": {
        "scene_material_class": "Default",
        "scene_material_class_id": 0,
        "reflection_preference": "NeutralFallback",
        "reflection_preference_id": 0,
        "temporal_policy": "StableDiffuse",
        "temporal_policy_id": 0,
        "post_sensitivity": "Normal",
        "post_sensitivity_id": 0,
    },
}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(data: dict[str, Any], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_text(text: str, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def material_family(material: str, role: str) -> str:
    text = f"{material} {role}".lower()
    for token, family in MATERIAL_FAMILY_TOKENS:
        if token in text:
            return family
    return "dielectric" if text.strip() else "unknown"


def texture_slots_for_features(feature_flags: list[str]) -> list[str]:
    return sorted(
        {
            slot
            for feature in feature_flags
            for slot in [FEATURE_TEXTURE_SLOTS.get(feature, "")]
            if slot
        }
    )


def runtime_policy_for_family(family: str) -> dict[str, Any]:
    return dict(RUNTIME_POLICY_BY_FAMILY.get(family, RUNTIME_POLICY_BY_FAMILY["unknown"]))


def registry_by_id(registry: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {str(asset.get("id", "")): asset for asset in registry.get("assets", [])}


def collect_binding_refs(bindings: dict[str, Any]) -> tuple[dict[str, list[dict[str, Any]]], list[dict[str, Any]]]:
    by_asset: dict[str, list[dict[str, Any]]] = defaultdict(list)
    primitives: list[dict[str, Any]] = []
    for scene in bindings.get("scenes", []):
        scene_id = str(scene.get("scene", ""))
        for binding in scene.get("bindings", []):
            ref = dict(binding)
            ref["scene"] = scene_id
            asset_id = str(binding.get("registry_asset_id", ""))
            if asset_id:
                by_asset[asset_id].append(ref)
            elif str(binding.get("kind", "")) == "primitive":
                primitives.append(ref)
    return by_asset, primitives


def infer_asset_evidence(asset: dict[str, Any], refs: list[dict[str, Any]]) -> dict[str, Any]:
    readiness = dict(asset.get("readiness", {}))
    families = sorted(
        {
            material_family(str(ref.get("material", "")), str(ref.get("role", "")))
            for ref in refs
        }
    )
    if not families:
        families = ["unknown"]

    feature_flags = sorted({flag for family in families for flag in FAMILY_FEATURES.get(family, set())})
    required_texture_slots = texture_slots_for_features(feature_flags)
    primary_family = families[0] if families else "unknown"
    runtime_policy = runtime_policy_for_family(primary_family)
    runtime_policy_candidates = {
        family: runtime_policy_for_family(family)
        for family in families
    }
    missing_texture_slots = (
        []
        if readiness.get("pbr_textures_complete", False)
        else required_texture_slots
    )
    hero_refs = [ref for ref in refs if bool(ref.get("hero_role", False))]
    blockers = list(asset.get("quality_blockers", []))
    if "unknown" in families:
        blockers.append("unknown material family")
    if not readiness.get("pbr_textures_complete", False):
        blockers.append("missing PBR texture evidence for V2 material model")
    if not readiness.get("lod_chain_ready", False):
        blockers.append("missing LOD evidence for V2 final asset")
    if not readiness.get("collision_proxy_ready", False):
        blockers.append("missing collision evidence for V2 final asset")
    if hero_refs and not readiness.get("pbr_textures_complete", False):
        blockers.append("hero surface lacks PBR texture readiness")
    if not feature_flags:
        blockers.append("missing shader feature flags")
    if missing_texture_slots:
        blockers.append("missing required V2 texture slots: " + ", ".join(missing_texture_slots))

    v2_material_ready = all(
        [
            "unknown" not in families,
            bool(feature_flags),
            readiness.get("pbr_textures_complete", False),
            readiness.get("provenance_ready", False),
            readiness.get("editable_mesh", False),
            readiness.get("separated_object", False),
        ]
    )

    return {
        "asset_id": asset.get("id", ""),
        "runtime_asset": asset.get("runtime_asset", ""),
        "source_class": asset.get("source_class", ""),
        "scene_families": asset.get("scene_families", []),
        "semantic_roles": asset.get("semantic_roles", []),
        "material_families": families,
        "primary_material_family": primary_family,
        "shader_feature_flags": feature_flags,
        "required_texture_slots": required_texture_slots,
        "missing_texture_slots": missing_texture_slots,
        "runtime_policy": runtime_policy,
        "runtime_policy_candidates": runtime_policy_candidates,
        "readiness": readiness,
        "v2_material_ready": v2_material_ready,
        "hero_surface_reference_count": len(hero_refs),
        "object_reference_count": len(refs),
        "blockers": sorted(set(blockers)),
    }


def scene_summary(scene: dict[str, Any]) -> dict[str, Any]:
    material_counts: Counter[str] = Counter()
    registry_bound = 0
    hero_registry_bound = 0
    primitive_hero = 0
    missing_registry_material = 0
    for binding in scene.get("bindings", []):
        family = material_family(str(binding.get("material", "")), str(binding.get("role", "")))
        material_counts[family] += 1
        if binding.get("registry_asset_id"):
            registry_bound += 1
            if binding.get("hero_role"):
                hero_registry_bound += 1
        elif binding.get("hero_role"):
            primitive_hero += 1
            missing_registry_material += 1
    return {
        "scene": scene.get("scene", ""),
        "status": scene.get("status", ""),
        "object_count": scene.get("object_count", 0),
        "registry_bound_count": registry_bound,
        "hero_registry_bound_count": hero_registry_bound,
        "primitive_hero_blocker_count": primitive_hero,
        "missing_registry_material_count": missing_registry_material,
        "material_family_counts": dict(sorted(material_counts.items())),
    }


def markdown_report(report: dict[str, Any]) -> str:
    lines = [
        "# Full Scene Shader Pipeline V2 Material Evidence",
        "",
        f"Status: `{report['status']}`",
        "",
        "## Summary",
        "",
    ]
    for key, value in report["summary"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Material Families", ""])
    for key, value in report["material_family_counts"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Runtime Policy Bridge", ""])
    for policy_group, counts in report["runtime_policy_counts"].items():
        lines.append(f"### {policy_group}")
        for key, value in counts.items():
            lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Scenes", ""])
    lines.append("| Scene | Objects | Registry Bound | Hero Registry Bound | Primitive Hero Blockers | Missing Registry Material |")
    lines.append("|---|---:|---:|---:|---:|---:|")
    for scene in report["scenes"]:
        lines.append(
            f"| {scene['scene']} | {scene['object_count']} | {scene['registry_bound_count']} | "
            f"{scene['hero_registry_bound_count']} | {scene['primitive_hero_blocker_count']} | "
            f"{scene['missing_registry_material_count']} |"
        )
    lines.extend(["", "## Top Blocked Assets", ""])
    blocked = [asset for asset in report["assets"] if not asset["v2_material_ready"]]
    for asset in blocked[:20]:
        blockers = "; ".join(asset["blockers"][:4])
        lines.append(f"- `{asset['asset_id']}`: {blockers}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--bindings", type=Path, default=DEFAULT_BINDINGS)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--md", type=Path, default=DEFAULT_MD)
    args = parser.parse_args()

    registry = load_json(args.registry)
    bindings = load_json(args.bindings)
    assets_by_id = registry_by_id(registry)
    refs_by_asset, primitives = collect_binding_refs(bindings)

    asset_reports = [
        infer_asset_evidence(asset, refs_by_asset.get(asset_id, []))
        for asset_id, asset in sorted(assets_by_id.items())
    ]

    material_family_counts: Counter[str] = Counter()
    feature_counts: Counter[str] = Counter()
    runtime_scene_class_counts: Counter[str] = Counter()
    runtime_reflection_counts: Counter[str] = Counter()
    runtime_temporal_counts: Counter[str] = Counter()
    runtime_post_counts: Counter[str] = Counter()
    for asset in asset_reports:
        for family in asset["material_families"]:
            material_family_counts[family] += 1
        for flag in asset["shader_feature_flags"]:
            feature_counts[flag] += 1
        runtime_scene_class_counts[asset["runtime_policy"]["scene_material_class"]] += 1
        runtime_reflection_counts[asset["runtime_policy"]["reflection_preference"]] += 1
        runtime_temporal_counts[asset["runtime_policy"]["temporal_policy"]] += 1
        runtime_post_counts[asset["runtime_policy"]["post_sensitivity"]] += 1

    scenes = [scene_summary(scene) for scene in bindings.get("scenes", [])]
    v2_ready_count = sum(1 for asset in asset_reports if asset["v2_material_ready"])
    pbr_ready_count = sum(
        1 for asset in asset_reports if asset["readiness"].get("pbr_textures_complete", False)
    )
    missing_hero_texture = sum(
        1
        for asset in asset_reports
        if asset["hero_surface_reference_count"] > 0
        and not asset["readiness"].get("pbr_textures_complete", False)
    )
    unknown_family_count = sum(1 for asset in asset_reports if "unknown" in asset["material_families"])
    primitive_hero_count = sum(1 for ref in primitives if ref.get("hero_role"))

    report = {
        "schema": "cortex.full_scene_shader_material_evidence_v2",
        "generated_by": "tools/build_full_scene_shader_material_evidence_v2.py",
        "source_registry": str(args.registry.relative_to(ROOT)).replace("\\", "/"),
        "source_bindings": str(args.bindings.relative_to(ROOT)).replace("\\", "/"),
        "status": "BLOCKED" if v2_ready_count < len(asset_reports) or primitive_hero_count else "READY",
        "summary": {
            "asset_count": len(asset_reports),
            "v2_material_ready_asset_count": v2_ready_count,
            "v2_material_ready_asset_ratio": round(v2_ready_count / max(1, len(asset_reports)), 4),
            "pbr_texture_ready_asset_count": pbr_ready_count,
            "missing_hero_texture_evidence_count": missing_hero_texture,
            "unknown_material_family_asset_count": unknown_family_count,
            "primitive_hero_material_blocker_count": primitive_hero_count,
            "runtime_policy_bridge_asset_count": sum(
                1 for asset in asset_reports if asset.get("runtime_policy")
            ),
            "scene_count": len(scenes),
        },
        "material_family_counts": dict(sorted(material_family_counts.items())),
        "shader_feature_flag_counts": dict(sorted(feature_counts.items())),
        "runtime_policy_counts": {
            "scene_material_class": dict(sorted(runtime_scene_class_counts.items())),
            "reflection_preference": dict(sorted(runtime_reflection_counts.items())),
            "temporal_policy": dict(sorted(runtime_temporal_counts.items())),
            "post_sensitivity": dict(sorted(runtime_post_counts.items())),
        },
        "assets": asset_reports,
        "scenes": scenes,
    }

    write_json(report, args.out)
    write_text(markdown_report(report), args.md)
    print(f"Wrote {args.out}")
    print(f"Wrote {args.md}")
    print(
        "Material evidence: "
        f"status={report['status']} assets={len(asset_reports)} "
        f"v2_ready={v2_ready_count} pbr_ready={pbr_ready_count}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
