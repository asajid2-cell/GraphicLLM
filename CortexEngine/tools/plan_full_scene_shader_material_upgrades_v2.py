#!/usr/bin/env python3
"""Plan material upgrades required by Full Scene Shader Pipeline V2."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EVIDENCE = ROOT / "assets/final_art/full_scene_shader_material_evidence_v2.json"
DEFAULT_BINDINGS = ROOT / "assets/final_art/scene_asset_bindings_v1.json"
DEFAULT_OUT_JSON = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.json"
)
DEFAULT_OUT_MD = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.md"
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


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(data: dict[str, Any], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_text(text: str, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def slug(value: str) -> str:
    return "".join(ch.lower() if ch.isalnum() else "_" for ch in value).strip("_")


def material_family(material: str, role: str) -> str:
    text = f"{material} {role}".lower()
    for token, family in MATERIAL_FAMILY_TOKENS:
        if token in text:
            return family
    return "dielectric" if text.strip() else "unknown"


def providers_for(families: list[str], kind: str) -> list[str]:
    family_set = set(families)
    if kind == "replace_primitive_hero_material_surface":
        return ["artist_authored_pbr", "cc0_curated_library", "hunyuan3d_2_1", "trellis_image_large"]
    if family_set & {"metal", "brushed_metal", "glass", "mirror", "screen", "emissive"}:
        return ["artist_authored_pbr", "hunyuan3d_2_1", "trellis_image_large"]
    if family_set & {"wood", "fabric", "plastic", "tile", "ceramic"}:
        return ["cc0_curated_library", "artist_authored_pbr", "hunyuan3d_2_1"]
    return ["artist_authored_pbr", "cc0_curated_library", "trellis_image_large"]


def needed_evidence(asset: dict[str, Any]) -> list[str]:
    readiness = asset.get("readiness", {})
    needed: list[str] = []
    if not readiness.get("pbr_textures_complete", False):
        needed.append("complete PBR texture set: base color, normal, ORM/roughness/metallic/AO")
    if not readiness.get("lod_chain_ready", False):
        needed.append("LOD0/LOD1/LOD2 chain registered in Asset Registry V2")
    if not readiness.get("collision_proxy_ready", False):
        needed.append("collision proxy registered in Asset Registry V2")
    if not readiness.get("preview_ready", False):
        needed.append("preview image and visual review evidence")
    if not asset.get("shader_feature_flags"):
        needed.append("shader feature flags for FullSceneMaterialModel")
    if asset.get("hero_surface_reference_count", 0) > 0:
        needed.append("hero-surface material review packet")
    return needed


def primitive_hero_orders(bindings: dict[str, Any]) -> list[dict[str, Any]]:
    orders: list[dict[str, Any]] = []
    for scene in bindings.get("scenes", []):
        scene_id = str(scene.get("scene", ""))
        for binding in scene.get("bindings", []):
            if not binding.get("hero_role") or binding.get("registry_asset_id"):
                continue
            if binding.get("binding_status") != "primitive_hero_blocker":
                continue
            role = str(binding.get("role", "unknown_role"))
            material = str(binding.get("material", "unknown_material"))
            object_id = str(binding.get("object", "unknown_object"))
            order_id = f"{slug(scene_id)}__primitive_hero_material__{slug(object_id)}"
            family = material_family(material, role)
            orders.append(
                {
                    "id": order_id,
                    "priority": "P0",
                    "kind": "replace_primitive_hero_material_surface",
                    "scene": scene_id,
                    "object": object_id,
                    "role": role,
                    "material": material,
                    "material_families": [family],
                    "reason": "hero surface is still a primitive/blockout material and cannot satisfy Full Scene Shader Pipeline V2",
                    "needed_evidence": [
                        "registry-backed replacement asset or admitted architecture material kit",
                        "complete PBR texture set",
                        "scale/support/contact anchors",
                        "LOD chain and collision proxy if object remains mesh-backed",
                    ],
                    "acceptance": [
                        "binding_status is no longer primitive_hero_blocker",
                        "replacement is represented in Asset Registry V2 or a V2 architecture material kit",
                        "material evidence report no longer counts this as primitive hero material blocker",
                        "renderer V1 visual/stability gates remain passing",
                    ],
                    "provider_preferences": providers_for([], "replace_primitive_hero_material_surface"),
                }
            )
    return orders


def asset_orders(evidence: dict[str, Any]) -> list[dict[str, Any]]:
    orders: list[dict[str, Any]] = []
    for asset in evidence.get("assets", []):
        if asset.get("v2_material_ready", False):
            continue
        families = list(asset.get("material_families", []))
        hero_refs = int(asset.get("hero_surface_reference_count", 0))
        priority = "P0" if hero_refs > 0 else "P1"
        kind = "upgrade_hero_asset_material_evidence" if hero_refs > 0 else "upgrade_registry_asset_material_evidence"
        order_id = f"shader_material__{slug(kind)}__{slug(str(asset.get('asset_id', 'asset')))}"
        orders.append(
            {
                "id": order_id,
                "priority": priority,
                "kind": kind,
                "asset": asset.get("asset_id", ""),
                "runtime_asset": asset.get("runtime_asset", ""),
                "source_class": asset.get("source_class", ""),
                "scene_families": asset.get("scene_families", []),
                "semantic_roles": asset.get("semantic_roles", []),
                "material_families": families,
                "primary_material_family": asset.get("primary_material_family", ""),
                "shader_feature_flags": asset.get("shader_feature_flags", []),
                "required_texture_slots": asset.get("required_texture_slots", []),
                "missing_texture_slots": asset.get("missing_texture_slots", []),
                "runtime_policy": asset.get("runtime_policy", {}),
                "runtime_policy_candidates": asset.get("runtime_policy_candidates", {}),
                "hero_surface_reference_count": hero_refs,
                "object_reference_count": asset.get("object_reference_count", 0),
                "reason": "registered asset lacks the material evidence required by Full Scene Shader Pipeline V2",
                "blockers": asset.get("blockers", []),
                "needed_evidence": needed_evidence(asset),
                "acceptance": [
                    "asset v2_material_ready becomes true or asset is replaced by a V2-ready registry entry",
                    "PBR texture readiness is explicit in Asset Registry V2",
                    "shader feature flags match the inferred material families",
                    "LOD/collision readiness is explicit before public final-art use",
                    "hero surfaces have preview/review evidence when referenced by hero roles",
                ],
                "provider_preferences": providers_for(families, kind),
            }
        )
    return orders


def markdown(payload: dict[str, Any]) -> str:
    lines = [
        "# Full Scene Shader Pipeline V2 Material Upgrade Work Orders",
        "",
        f"Status: `{payload['status']}`",
        "",
        "## Summary",
        "",
    ]
    for key, value in payload["summary"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Priority Counts", ""])
    for key, value in payload["priority_counts"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Top Orders", ""])
    for order in payload["work_orders"][:40]:
        lines.append(f"### {order['id']}")
        lines.append(f"- priority: `{order['priority']}`")
        lines.append(f"- kind: `{order['kind']}`")
        if "scene" in order:
            lines.append(f"- scene: `{order['scene']}`")
        if "asset" in order:
            lines.append(f"- asset: `{order['asset']}`")
            lines.append(f"- hero refs: `{order.get('hero_surface_reference_count', 0)}`")
        lines.append(f"- reason: {order['reason']}")
        if order.get("needed_evidence"):
            lines.append(f"- needed: {'; '.join(order['needed_evidence'][:4])}")
        lines.append(f"- providers: {', '.join(order['provider_preferences'])}")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path, default=DEFAULT_EVIDENCE)
    parser.add_argument("--bindings", type=Path, default=DEFAULT_BINDINGS)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    evidence = load_json(args.evidence)
    bindings = load_json(args.bindings)
    orders = primitive_hero_orders(bindings) + asset_orders(evidence)
    priority_rank = {"P0": 0, "P1": 1, "P2": 2}
    orders.sort(
        key=lambda order: (
            priority_rank.get(order["priority"], 99),
            -int(order.get("hero_surface_reference_count", 0)),
            order["id"],
        )
    )
    priority_counts = Counter(order["priority"] for order in orders)
    kind_counts = Counter(order["kind"] for order in orders)
    payload = {
        "schema": "cortex.full_scene_shader_material_upgrade_plan_v2",
        "generated_by": "tools/plan_full_scene_shader_material_upgrades_v2.py",
        "source_material_evidence": args.evidence.relative_to(ROOT).as_posix(),
        "source_bindings": args.bindings.relative_to(ROOT).as_posix(),
        "status": "READY" if orders else "EMPTY",
        "summary": {
            "work_order_count": len(orders),
            "p0_count": priority_counts.get("P0", 0),
            "p1_count": priority_counts.get("P1", 0),
            "primitive_hero_material_order_count": kind_counts.get(
                "replace_primitive_hero_material_surface", 0
            ),
            "hero_asset_material_order_count": kind_counts.get(
                "upgrade_hero_asset_material_evidence", 0
            ),
            "registry_asset_material_order_count": kind_counts.get(
                "upgrade_registry_asset_material_evidence", 0
            ),
        },
        "priority_counts": dict(sorted(priority_counts.items())),
        "kind_counts": dict(sorted(kind_counts.items())),
        "work_orders": orders,
    }

    write_json(payload, args.out_json)
    write_text(markdown(payload), args.out_md)
    print(
        json.dumps(
            {
                "status": payload["status"],
                "work_order_count": len(orders),
                "p0_count": payload["summary"]["p0_count"],
                "report": args.out_json.relative_to(ROOT).as_posix(),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
