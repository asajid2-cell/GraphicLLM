#!/usr/bin/env python3
"""Build scene seed object bindings to Asset Registry V2 entries."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONTRACT = ROOT / "assets/final_art/aaa_asset_quality_contract.json"
DEFAULT_REGISTRY = ROOT / "assets/final_art/asset_registry_v2.json"
DEFAULT_OUT = ROOT / "assets/final_art/scene_asset_bindings_v1.json"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(value, indent=2) + "\n")


def normalize(path: str) -> str:
    return path.replace("\\", "/").strip()


def is_runtime_asset(path: str) -> bool:
    return normalize(path).lower().endswith((".gltf", ".glb"))


def registry_index(registry: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        normalize(str(asset.get("runtime_asset", ""))): asset
        for asset in registry.get("assets", [])
        if asset.get("runtime_asset")
    }


def classify_object(
    obj: dict[str, Any],
    registry_by_path: dict[str, dict[str, Any]],
    hero_roles: set[str],
    blockout_allowlist_roles: set[str],
) -> dict[str, Any]:
    role = str(obj.get("role", ""))
    runtime_asset = normalize(str(obj.get("runtime_asset", "")))
    primitive = bool(obj.get("primitive")) or str(obj.get("kind", "")) == "primitive"
    bound = registry_by_path.get(runtime_asset) if is_runtime_asset(runtime_asset) else None
    if bound:
        status = "registry_bound"
    elif is_runtime_asset(runtime_asset):
        status = "unresolved_runtime_asset"
    elif primitive and role in hero_roles:
        status = "primitive_hero_blocker"
    elif primitive and role in blockout_allowlist_roles:
        status = "primitive_blockout_allowed"
    elif primitive:
        status = "primitive_scene_detail"
    else:
        status = "non_asset_object"
    return {
        "object": obj.get("id", ""),
        "role": role,
        "kind": obj.get("kind", ""),
        "primitive": obj.get("primitive", ""),
        "runtime_asset": runtime_asset,
        "registry_asset_id": bound.get("id", "") if bound else "",
        "registry_source_class": bound.get("source_class", "") if bound else "",
        "registry_aaa_ready": bool((bound or {}).get("readiness", {}).get("aaa_ready", False)),
        "binding_status": status,
        "blockout_allowed": role in blockout_allowlist_roles,
        "hero_role": role in hero_roles,
        "material": obj.get("material", ""),
        "support": obj.get("support", ""),
        "assembly_id": obj.get("assembly_id", ""),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--registry", type=Path, default=DEFAULT_REGISTRY)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    contract = load_json(args.contract)
    registry = load_json(args.registry)
    registry_by_path = registry_index(registry)
    scenes: list[dict[str, Any]] = []
    summary = {
        "object_count": 0,
        "registry_bound_count": 0,
        "primitive_blockout_count": 0,
        "primitive_hero_blocker_count": 0,
        "unresolved_runtime_asset_count": 0,
        "aaa_ready_bound_count": 0,
    }
    for target in contract.get("target_scenes", []):
        seed_raw = str(target.get("admitted_seed", ""))
        seed_path = ROOT / seed_raw if seed_raw else None
        hero_roles = set(str(role) for role in target.get("hero_roles", []))
        blockout_allowlist = set(str(role) for role in target.get("blockout_allowlist_roles", []))
        if not seed_path or not seed_path.exists():
            scenes.append(
                {
                    "scene": target.get("id", ""),
                    "seed": seed_raw,
                    "status": "MISSING_SEED",
                    "object_count": 0,
                    "registry_bound_count": 0,
                    "primitive_blockout_count": 0,
                    "primitive_hero_blocker_count": 0,
                    "unresolved_runtime_asset_count": 0,
                    "aaa_ready_bound_count": 0,
                    "bindings": [],
                }
            )
            continue
        seed = load_json(seed_path)
        bindings = [
            classify_object(obj, registry_by_path, hero_roles, blockout_allowlist)
            for obj in seed.get("objects", [])
        ]
        scene_summary = {
            "scene": target.get("id", ""),
            "seed": seed_raw,
            "status": "BOUND",
            "object_count": len(bindings),
            "registry_bound_count": sum(1 for item in bindings if item["binding_status"] == "registry_bound"),
            "primitive_blockout_count": sum(1 for item in bindings if item["binding_status"] == "primitive_blockout_allowed"),
            "primitive_hero_blocker_count": sum(1 for item in bindings if item["binding_status"] == "primitive_hero_blocker"),
            "unresolved_runtime_asset_count": sum(1 for item in bindings if item["binding_status"] == "unresolved_runtime_asset"),
            "aaa_ready_bound_count": sum(1 for item in bindings if item["registry_aaa_ready"]),
            "bindings": bindings,
        }
        for key in summary:
            summary[key] += int(scene_summary.get(key, 0))
        scenes.append(scene_summary)
    output = {
        "schema": "cortex.scene_asset_bindings_v1",
        "generated_by": "tools/build_scene_asset_bindings_v1.py",
        "source_registry": args.registry.relative_to(ROOT).as_posix(),
        "scene_count": len(scenes),
        "summary": summary,
        "scenes": scenes,
    }
    write_json(args.out, output)
    print(json.dumps({"status": "PASS", "bindings": args.out.relative_to(ROOT).as_posix(), **summary}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
