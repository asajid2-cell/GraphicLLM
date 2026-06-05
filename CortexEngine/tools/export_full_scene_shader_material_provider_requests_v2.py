#!/usr/bin/env python3
"""Export Full Scene Shader Pipeline V2 material upgrade requests."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORK_ORDERS = (
    ROOT / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/material_upgrade_work_orders.json"
)
DEFAULT_OUT_ROOT = (
    ROOT / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests"
)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def pbr_maps_for(families: list[str]) -> list[str]:
    family_set = set(families)
    maps = ["base_color", "normal", "orm_or_separate_occlusion_roughness_metallic"]
    if family_set & {"emissive", "screen"}:
        maps.append("emissive")
    if family_set & {"glass", "water"}:
        maps.append("opacity_or_transmission")
    if family_set & {"wood", "fabric", "tile", "painted_wall", "water"}:
        maps.append("detail_normal_or_height_optional")
    return maps


def prompt_for(order: dict[str, Any]) -> str:
    families = ", ".join(order.get("material_families", [])) or "physically based"
    if order["kind"] == "replace_primitive_hero_material_surface":
        return (
            f"Create an AAA game-ready replacement material/asset surface for "
            f"{order.get('scene', 'scene')} object {order.get('object', '')} "
            f"role {order.get('role', '')}. Use {families} material treatment, "
            "clean scale, separated editable mesh or architecture material kit, "
            "complete PBR maps, support/contact anchors, LODs, collision, and preview."
        )
    return (
        f"Upgrade existing runtime asset {order.get('asset', '')} for Full Scene Shader Pipeline V2. "
        f"Target material families: {families}. Provide complete PBR material evidence, "
        "shader feature flags, LOD0/LOD1/LOD2, collision proxy, preview render, and provenance."
    )


def request_for_order(order: dict[str, Any]) -> dict[str, Any]:
    families = list(order.get("material_families", []))
    shader_flags = list(order.get("shader_feature_flags", []))
    if not families and order["kind"] == "replace_primitive_hero_material_surface":
        families = ["dielectric"]
        shader_flags = ["base_color_texture", "normal_texture", "orm_texture"]

    request: dict[str, Any] = {
        "schema": "cortex.full_scene_shader_material_provider_request.v2",
        "id": order["id"],
        "priority": order["priority"],
        "source_work_order": order["id"],
        "kind": order["kind"],
        "provider_preferences": order.get("provider_preferences", []),
        "prompt": prompt_for(order),
        "negative_prompt": (
            "low detail, melted geometry, fused scene, whole-room mesh, no UVs, "
            "missing normal map, missing roughness/metallic/AO, wrong scale, text labels, watermark"
        ),
        "accepted_outputs": ["gltf", "glb", "png_pbr_texture_set", "json_manifest"],
        "forbidden_outputs": [
            "whole_scene_mesh",
            "single_uneditable_blob",
            "gaussian_only",
            "radiance_field_only",
            "screenshots_without_assets",
        ],
        "material_contract": {
            "material_families": families,
            "shader_feature_flags": shader_flags,
            "required_pbr_maps": pbr_maps_for(families),
            "texture_resolution_min": 1024,
            "texture_resolution_target": 2048,
            "uvs_required": True,
            "tiling_or_triplanar_metadata_required_for_architecture_surfaces": True,
            "hero_surface_review_required": order.get("priority") == "P0",
        },
        "asset_registry_update_contract": {
            "must_update_asset_registry_v2": True,
            "must_set_pbr_textures_complete": True,
            "must_register_shader_feature_flags": True,
            "must_register_lod_chain": True,
            "must_register_collision_proxy": True,
            "must_register_preview": True,
            "must_register_provenance": True,
        },
        "admission_gate": {
            "must_clear_source_work_order": True,
            "must_improve_full_scene_shader_material_evidence_v2": True,
            "must_not_regress_aaa_asset_quality": True,
            "must_preserve_renderer_v1_packet_gates": True,
            "must_render_in_v2_material_debug_packet": True,
        },
    }
    for key in [
        "scene",
        "object",
        "role",
        "material",
        "asset",
        "runtime_asset",
        "source_class",
        "scene_families",
        "semantic_roles",
        "hero_surface_reference_count",
        "object_reference_count",
        "needed_evidence",
        "blockers",
    ]:
        if key in order:
            request[key] = order[key]
    return request


def manifest_markdown(manifest: dict[str, Any]) -> str:
    lines = [
        "# Full Scene Shader Pipeline V2 Provider Requests",
        "",
        f"Request count: `{manifest['request_count']}`",
        "",
        "## Priority Counts",
        "",
    ]
    for priority, count in manifest["priority_counts"].items():
        lines.append(f"- `{priority}`: `{count}`")
    lines.extend(["", "## Kind Counts", ""])
    for kind, count in manifest["kind_counts"].items():
        lines.append(f"- `{kind}`: `{count}`")
    lines.extend(["", "## Requests", ""])
    for request in manifest["requests"]:
        lines.append(f"### {request['id']}")
        lines.append(f"- priority: `{request['priority']}`")
        lines.append(f"- kind: `{request['kind']}`")
        if request.get("scene"):
            lines.append(f"- scene: `{request['scene']}`")
        if request.get("asset"):
            lines.append(f"- asset: `{request['asset']}`")
        if request.get("object"):
            lines.append(f"- object: `{request['object']}`")
        lines.append(f"- providers: {', '.join(request.get('provider_preferences', []))}")
        lines.append(f"- pack: `{request['request_path']}`")
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-orders", type=Path, default=DEFAULT_WORK_ORDERS)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT_ROOT)
    parser.add_argument("--priority", action="append", default=["P0", "P1"])
    args = parser.parse_args()

    work_orders = load_json(args.work_orders)
    priorities = set(args.priority)
    requests: list[dict[str, Any]] = []
    for order in work_orders.get("work_orders", []):
        if order.get("priority") not in priorities:
            continue
        request = request_for_order(order)
        request_path = args.out_root / request["priority"].lower() / f"{request['id']}.json"
        write_json(request_path, request)
        request["request_path"] = request_path.relative_to(ROOT).as_posix()
        requests.append(request)

    priority_counts = Counter(request["priority"] for request in requests)
    kind_counts = Counter(request["kind"] for request in requests)
    manifest = {
        "schema": "cortex.full_scene_shader_material_provider_request_manifest.v2",
        "generated_by": "tools/export_full_scene_shader_material_provider_requests_v2.py",
        "source_work_orders": args.work_orders.relative_to(ROOT).as_posix(),
        "request_count": len(requests),
        "priority_counts": dict(sorted(priority_counts.items())),
        "kind_counts": dict(sorted(kind_counts.items())),
        "requests": requests,
    }
    write_json(args.out_root / "manifest.json", manifest)
    write_text(args.out_root / "manifest.md", manifest_markdown(manifest))
    print(
        json.dumps(
            {
                "status": "PASS",
                "request_count": len(requests),
                "manifest": (args.out_root / "manifest.json").relative_to(ROOT).as_posix(),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
