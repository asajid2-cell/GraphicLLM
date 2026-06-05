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
        "shader_feature_flags": feature_flags,
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
    for asset in asset_reports:
        for family in asset["material_families"]:
            material_family_counts[family] += 1
        for flag in asset["shader_feature_flags"]:
            feature_counts[flag] += 1

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
            "scene_count": len(scenes),
        },
        "material_family_counts": dict(sorted(material_family_counts.items())),
        "shader_feature_flag_counts": dict(sorted(feature_counts.items())),
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
