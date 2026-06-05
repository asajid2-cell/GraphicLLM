#!/usr/bin/env python3
"""Convert AAA asset-quality blockers into concrete replacement work orders."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "docs/media/final_art/generated/aaa_asset_quality/aaa_asset_quality_report.json"
DEFAULT_REGISTRY = ROOT / "assets/final_art/asset_registry_v2.json"
DEFAULT_OUT_JSON = ROOT / "docs/media/final_art/generated/aaa_asset_quality/aaa_asset_replacement_work_orders.json"
DEFAULT_OUT_MD = ROOT / "docs/media/final_art/generated/aaa_asset_quality/aaa_asset_replacement_work_orders.md"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def slug(value: str) -> str:
    return "".join(ch.lower() if ch.isalnum() else "_" for ch in value).strip("_")


def replacement_strategy(scene_id: str, role: str) -> dict[str, Any]:
    role_lower = role.lower()
    if any(token in role_lower for token in ["wall", "floor", "ceiling", "venue", "court"]):
        source = "modular_architecture_kit_or_artist_authored_pbr"
        provider = ["cc0_curated_library", "artist_authored_pbr"]
    elif any(token in role_lower for token in ["chair", "seat", "desk", "table", "cabinet", "sink", "monitor", "keyboard", "book", "shelf"]):
        source = "curated_prop_library_then_provider_gap_fill"
        provider = ["cc0_curated_library", "hunyuan3d_2_1", "trellis_image_large"]
    elif any(token in role_lower for token in ["hoop", "backboard", "ball", "scoreboard", "stage", "screen", "light"]):
        source = "hero_fixture_asset_request"
        provider = ["artist_authored_pbr", "hunyuan3d_2_1", "trellis_image_large"]
    else:
        source = "semantic_asset_request"
        provider = ["hunyuan3d_2_1", "trellis_image_large", "cc0_curated_library"]
    return {
        "source_strategy": source,
        "provider_preferences": provider,
        "prompt": f"AAA game-ready PBR {role.replace('_', ' ')} asset for {scene_id.replace('_', ' ')}, separated editable mesh, correct scale, clean silhouette, complete material maps, LOD0/LOD1/LOD2 and collision proxy",
    }


def role_orders(report: dict[str, Any]) -> list[dict[str, Any]]:
    orders: list[dict[str, Any]] = []
    for scene in report.get("scenes", []):
        scene_id = str(scene["id"])
        for role in scene.get("primitive_hero_roles", []):
            strategy = replacement_strategy(scene_id, role)
            orders.append(
                {
                    "id": f"{slug(scene_id)}__replace_primitive_hero__{slug(role)}",
                    "priority": "P0",
                    "scene": scene_id,
                    "kind": "replace_primitive_hero_role",
                    "role": role,
                    "reason": "hero role is still primitive/blockout",
                    "acceptance": [
                        "runtime asset comes from Asset Registry V2",
                        "asset has complete PBR material set",
                        "asset has LOD chain",
                        "asset has collision proxy",
                        "asset has support/contact anchors",
                        "role is no longer counted as primitive in AAA report",
                    ],
                    **strategy,
                }
            )
        for role in scene.get("missing_required_roles", []):
            strategy = replacement_strategy(scene_id, role)
            orders.append(
                {
                    "id": f"{slug(scene_id)}__add_missing_required_role__{slug(role)}",
                    "priority": "P0",
                    "scene": scene_id,
                    "kind": "add_missing_required_role",
                    "role": role,
                    "reason": "required catalog role is not represented in the admitted seed",
                    "acceptance": [
                        "scene seed contains at least one object with this role",
                        "role object is visible in a validation camera",
                        "role object is registry-backed when it is not a shell primitive",
                        "AAA required role coverage meets minimum",
                    ],
                    **strategy,
                }
            )
    return orders


def registry_orders(registry: dict[str, Any], limit: int) -> list[dict[str, Any]]:
    assets = list(registry.get("assets", []))
    assets.sort(key=lambda asset: (-len(asset.get("referenced_by", [])), asset.get("id", "")))
    orders: list[dict[str, Any]] = []
    for asset in assets[:limit]:
        blockers = list(asset.get("quality_blockers", []))
        if not blockers:
            continue
        roles = list(asset.get("semantic_roles", []))
        scene_counts = Counter(ref.get("scene", "") for ref in asset.get("referenced_by", []))
        orders.append(
            {
                "id": f"registry__upgrade__{asset['id']}",
                "priority": "P1",
                "kind": "upgrade_existing_registry_asset",
                "asset": asset["id"],
                "runtime_asset": asset["runtime_asset"],
                "source_class": asset["source_class"],
                "referenced_by_count": len(asset.get("referenced_by", [])),
                "scenes": sorted(scene for scene in scene_counts if scene),
                "roles": roles,
                "reason": "existing runtime mesh is useful but not AAA-ready",
                "blockers": blockers,
                "acceptance": [
                    "registry readiness aaa_ready becomes true or asset is replaced",
                    "PBR texture completeness is explicit",
                    "LOD chain paths are registered",
                    "collision proxy path is registered",
                    "preview and provenance are registered",
                ],
            }
        )
    return orders


def write_markdown(payload: dict[str, Any], path: Path) -> None:
    lines: list[str] = []
    lines.append("# AAA Asset Replacement Work Orders")
    lines.append("")
    lines.append(f"Status: `{payload['status']}`")
    lines.append(f"Work orders: `{payload['work_order_count']}`")
    lines.append("")
    lines.append("## Top Orders")
    lines.append("")
    for order in payload["work_orders"]:
        lines.append(f"### {order['id']}")
        lines.append(f"- priority: `{order['priority']}`")
        lines.append(f"- kind: `{order['kind']}`")
        if "scene" in order:
            lines.append(f"- scene: `{order['scene']}`")
        if "role" in order:
            lines.append(f"- role: `{order['role']}`")
        if "asset" in order:
            lines.append(f"- asset: `{order['asset']}`")
            lines.append(f"- references: `{order['referenced_by_count']}`")
        lines.append(f"- reason: {order['reason']}")
        if order.get("blockers"):
            lines.append(f"- blockers: {', '.join(order['blockers'])}")
        if order.get("provider_preferences"):
            lines.append(f"- providers: {', '.join(order['provider_preferences'])}")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--registry-limit", type=int, default=20)
    args = parser.parse_args()

    report = load_json(args.report)
    registry = load_json(args.registry)
    orders = role_orders(report) + registry_orders(registry, args.registry_limit)
    priority_rank = {"P0": 0, "P1": 1, "P2": 2}
    orders.sort(key=lambda order: (priority_rank.get(order["priority"], 99), order["id"]))
    payload = {
        "schema": "cortex.aaa_asset_replacement_work_orders.v1",
        "status": "READY" if orders else "EMPTY",
        "source_report": args.report.relative_to(ROOT).as_posix(),
        "source_registry": args.registry.relative_to(ROOT).as_posix(),
        "work_order_count": len(orders),
        "work_orders": orders,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    with args.out_json.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(payload, indent=2) + "\n")
    write_markdown(payload, args.out_md)
    print(json.dumps({"status": payload["status"], "work_order_count": len(orders), "report": args.out_json.relative_to(ROOT).as_posix()}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
