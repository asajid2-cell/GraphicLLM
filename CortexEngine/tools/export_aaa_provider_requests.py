#!/usr/bin/env python3
"""Export AAA replacement work orders as provider/library request packs."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_WORK_ORDERS = ROOT / "docs/media/final_art/generated/aaa_asset_quality/aaa_asset_replacement_work_orders.json"
DEFAULT_OUT_ROOT = ROOT / "docs/media/final_art/generated/aaa_asset_quality/provider_requests"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(value, indent=2) + "\n")


def request_for_order(order: dict[str, Any]) -> dict[str, Any]:
    is_role_order = order["kind"] in {"replace_primitive_hero_role", "add_missing_required_role"}
    if is_role_order:
        role = str(order.get("role", "asset"))
        scene = str(order.get("scene", "scene"))
        return {
            "schema": "cortex.aaa_provider_request.v1",
            "id": order["id"],
            "priority": order["priority"],
            "source_work_order": order["id"],
            "scene": scene,
            "role": role,
            "kind": "new_or_replacement_asset",
            "provider_preferences": order.get("provider_preferences", []),
            "prompt": order.get("prompt", ""),
            "negative_prompt": "low detail, melted geometry, fused room, single scene blob, wrong scale, text labels, watermark, no material separation, no collision proxy",
            "accepted_outputs": ["gltf", "glb"],
            "forbidden_outputs": ["whole_scene_mesh", "gaussian_only", "radiance_field_only", "uneditable_blob"],
            "asset_contract": {
                "editable_separated_mesh": True,
                "complete_pbr_textures": True,
                "lod_chain_required": True,
                "collision_proxy_required": True,
                "preview_required": True,
                "scale_bounds_required": True,
                "support_contact_anchors_required": True,
                "semantic_role": role,
            },
            "admission_gate": {
                "must_register_in_asset_registry_v2": True,
                "must_clear_source_work_order": True,
                "must_improve_aaa_asset_quality_report": True,
                "must_render_in_scene_packet": True,
            },
        }
    return {
        "schema": "cortex.aaa_provider_request.v1",
        "id": order["id"],
        "priority": order["priority"],
        "source_work_order": order["id"],
        "kind": "upgrade_existing_asset",
        "asset": order.get("asset", ""),
        "runtime_asset": order.get("runtime_asset", ""),
        "source_class": order.get("source_class", ""),
        "scenes": order.get("scenes", []),
        "roles": order.get("roles", []),
        "upgrade_contract": {
            "complete_pbr_textures": "author or acquire texture set and register paths",
            "lod_chain": "generate/register LOD0 LOD1 LOD2",
            "collision_proxy": "generate/register simplified collision mesh",
            "preview": "render and register preview image",
            "provenance": "record license/provider/source metadata",
        },
        "admission_gate": {
            "asset_registry_v2_readiness_aaa_ready": True,
            "must_improve_aaa_asset_quality_report": True,
        },
    }


def write_markdown(manifest: dict[str, Any], path: Path) -> None:
    lines = [
        "# AAA Provider Request Manifest",
        "",
        f"Request count: `{manifest['request_count']}`",
        f"P0 count: `{manifest['priority_counts'].get('P0', 0)}`",
        f"P1 count: `{manifest['priority_counts'].get('P1', 0)}`",
        "",
        "## Requests",
        "",
    ]
    for request in manifest["requests"]:
        lines.append(f"### {request['id']}")
        lines.append(f"- priority: `{request['priority']}`")
        lines.append(f"- kind: `{request['kind']}`")
        if request.get("scene"):
            lines.append(f"- scene: `{request['scene']}`")
        if request.get("role"):
            lines.append(f"- role: `{request['role']}`")
        if request.get("asset"):
            lines.append(f"- asset: `{request['asset']}`")
        if request.get("provider_preferences"):
            lines.append(f"- providers: {', '.join(request['provider_preferences'])}")
        lines.append(f"- pack: `{request['request_path']}`")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--work-orders", type=Path, default=DEFAULT_WORK_ORDERS)
    parser.add_argument("--out-root", type=Path, default=DEFAULT_OUT_ROOT)
    parser.add_argument("--priority", action="append", default=["P0", "P1"])
    args = parser.parse_args()

    work_orders = load_json(args.work_orders)
    priorities = set(args.priority)
    requests: list[dict[str, Any]] = []
    priority_counts: dict[str, int] = {}
    for order in work_orders.get("work_orders", []):
        if order.get("priority") not in priorities:
            continue
        request = request_for_order(order)
        priority_counts[request["priority"]] = priority_counts.get(request["priority"], 0) + 1
        request_path = args.out_root / f"{request['priority'].lower()}" / f"{request['id']}.json"
        write_json(request_path, request)
        request["request_path"] = request_path.relative_to(ROOT).as_posix()
        requests.append(request)

    manifest = {
        "schema": "cortex.aaa_provider_request_manifest.v1",
        "source_work_orders": args.work_orders.relative_to(ROOT).as_posix(),
        "request_count": len(requests),
        "priority_counts": priority_counts,
        "requests": requests,
    }
    write_json(args.out_root / "manifest.json", manifest)
    write_markdown(manifest, args.out_root / "manifest.md")
    print(json.dumps({"status": "PASS", "request_count": len(requests), "manifest": (args.out_root / "manifest.json").relative_to(ROOT).as_posix()}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
