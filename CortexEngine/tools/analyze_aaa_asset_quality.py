#!/usr/bin/env python3
"""Analyze whether target scenes satisfy the AAA asset-quality contract."""

from __future__ import annotations

import argparse
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONTRACT = ROOT / "assets/final_art/aaa_asset_quality_contract.json"
DEFAULT_CATALOG = ROOT / "assets/final_art/final_art_scene_catalog.json"
DEFAULT_IMPORT_MANIFEST = ROOT / "assets/generated/pretrained_assets/import_manifest.json"
DEFAULT_ASSET_REGISTRY = ROOT / "assets/final_art/asset_registry_v2.json"
DEFAULT_OUT_JSON = ROOT / "docs/media/final_art/generated/aaa_asset_quality/aaa_asset_quality_report.json"
DEFAULT_OUT_MD = ROOT / "docs/media/final_art/generated/aaa_asset_quality/aaa_asset_quality_report.md"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def ratio(numerator: float, denominator: float) -> float:
    return 0.0 if denominator <= 0.0 else float(numerator) / float(denominator)


def is_real_runtime_asset(path: str) -> bool:
    if not path:
        return False
    normalized = path.replace("\\", "/")
    return normalized.endswith((".gltf", ".glb"))


def is_prototype_generated_asset(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return "openai_shap_e_text300m" in normalized or "shap_e" in normalized.lower()


def is_fidelity_mesh(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return "assets/generated/final_art_fidelity_meshes/" in normalized


def is_curated_asset(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return (
        "assets/models/kenney_furniture_kit/" in normalized
        or "assets/models/naturalistic_showcase/" in normalized
        or "assets/generated/pretrained_assets/" in normalized
    )


def imported_asset_index(import_manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    by_scene_role: dict[str, dict[str, Any]] = {}
    for asset in import_manifest.get("assets", []):
        scene = str(asset.get("scene", ""))
        request_id = str(asset.get("request_id", ""))
        by_scene_role[f"{scene}:{request_id}"] = asset
    return by_scene_role


def asset_registry_index(asset_registry: dict[str, Any] | None) -> dict[str, dict[str, Any]]:
    if not asset_registry:
        return {}
    return {
        str(asset.get("runtime_asset", "")).replace("\\", "/"): asset
        for asset in asset_registry.get("assets", [])
        if asset.get("runtime_asset")
    }


def renderer_family_evidence(renderer_manifest: dict[str, Any] | None) -> dict[str, dict[str, Any]]:
    if not renderer_manifest:
        return {}
    evidence: dict[str, dict[str, Any]] = {}
    for result in renderer_manifest.get("results", []):
        if result.get("view") != "beauty":
            continue
        family = str(result.get("family", ""))
        contract = result.get("scene_visual_contract") or {}
        evidence[family] = {
            "capture": result.get("capture", ""),
            "external_hdri_visible": bool(contract.get("external_hdri_visible", False)),
            "invalid_external_hdri": bool(contract.get("invalid_external_hdri", False)),
            "environment_owner": contract.get("environment_owner", ""),
            "reflection_owner": contract.get("reflection_owner", ""),
            "profile_id": contract.get("profile_id", ""),
        }
    return evidence


def scene_catalog_index(catalog: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {str(scene.get("id", "")): scene for scene in catalog.get("scenes", [])}


def analyze_scene(
    target: dict[str, Any],
    contract: dict[str, Any],
    catalog_by_id: dict[str, dict[str, Any]],
    renderer_by_family: dict[str, dict[str, Any]],
    registry_by_path: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    minimums = contract["minimums"]
    weights = contract["weights"]
    seed_path_raw = str(target.get("admitted_seed", ""))
    seed_path = ROOT / seed_path_raw if seed_path_raw else None
    blockers: list[str] = []
    warnings: list[str] = []

    seed: dict[str, Any] | None = None
    if seed_path and seed_path.exists():
        seed = load_json(seed_path)
    elif target.get("renderer_family") != "gallery":
        blockers.append("missing target seed")

    renderer_family = str(target.get("renderer_family", ""))
    renderer = renderer_by_family.get(renderer_family)
    if renderer_by_family and not renderer:
        blockers.append("renderer family missing from manifest")
    if renderer:
        if renderer["invalid_external_hdri"]:
            blockers.append("renderer marks external HDRI invalid")
        if renderer["external_hdri_visible"] and renderer_family != "gallery":
            blockers.append("enclosed scene permits visible HDRI")

    catalog_scene = catalog_by_id.get(str(target.get("catalog_scene", "")), {})
    required_roles = set(str(role) for role in catalog_scene.get("required_roles", []))
    hero_roles = set(str(role) for role in target.get("hero_roles", []))
    blockout_allowlist = set(str(role) for role in target.get("blockout_allowlist_roles", []))

    objects = list((seed or {}).get("objects", []))
    roles = Counter(str(obj.get("role", "")) for obj in objects)
    material_count = len((seed or {}).get("materials", {}) or {})
    object_count = len(objects)
    runtime_assets = [str(obj.get("runtime_asset", "")) for obj in objects if is_real_runtime_asset(str(obj.get("runtime_asset", "")))]
    unique_runtime_assets = set(runtime_assets)
    primitive_objects = [obj for obj in objects if str(obj.get("kind", "")) == "primitive" or obj.get("primitive")]
    non_allowlisted_primitives = [
        obj for obj in primitive_objects if str(obj.get("role", "")) not in blockout_allowlist
    ]
    hero_objects = [obj for obj in objects if str(obj.get("role", "")) in hero_roles]
    hero_runtime = [obj for obj in hero_objects if is_real_runtime_asset(str(obj.get("runtime_asset", "")))]
    missing_required_roles = sorted(role for role in required_roles if roles[role] <= 0)
    primitive_hero_roles = sorted(
        {
            str(obj.get("role", ""))
            for obj in hero_objects
            if not is_real_runtime_asset(str(obj.get("runtime_asset", "")))
        }
    )
    runtime_mesh_ratio = ratio(len(runtime_assets), object_count)
    primitive_ratio = ratio(len(non_allowlisted_primitives), max(1, object_count))
    hero_runtime_ratio = ratio(len(hero_runtime), max(1, len(hero_objects)))
    required_role_coverage = ratio(len(required_roles) - len(missing_required_roles), max(1, len(required_roles)))
    unique_runtime_asset_ratio = clamp01(ratio(len(unique_runtime_assets), minimums["min_unique_runtime_assets"]))
    material_preset_ratio = clamp01(ratio(material_count, minimums["min_material_presets"]))
    detail_object_ratio = clamp01(ratio(object_count, minimums["min_scene_detail_objects"]))

    if registry_by_path:
        registry_entries = [registry_by_path.get(asset) for asset in runtime_assets]
        registry_entries = [entry for entry in registry_entries if entry]
        pbr_ready_assets = [entry for entry in registry_entries if (entry.get("readiness") or {}).get("pbr_textures_complete")]
        lod_ready_assets = [entry for entry in registry_entries if (entry.get("readiness") or {}).get("lod_chain_ready")]
        collision_ready_assets = [entry for entry in registry_entries if (entry.get("readiness") or {}).get("collision_proxy_ready")]
        provenance_ready_assets = [entry for entry in registry_entries if (entry.get("readiness") or {}).get("provenance_ready")]
    else:
        # Fallback when registry V2 has not been built yet.
        pbr_ready_assets = [asset for asset in runtime_assets if not is_fidelity_mesh(asset) and not is_prototype_generated_asset(asset)]
        lod_ready_assets = [asset for asset in runtime_assets if is_curated_asset(asset) and not is_prototype_generated_asset(asset)]
        collision_ready_assets = [asset for asset in runtime_assets if is_curated_asset(asset) and not is_prototype_generated_asset(asset)]
        provenance_ready_assets = [asset for asset in runtime_assets if is_curated_asset(asset) or is_fidelity_mesh(asset)]

    pbr_texture_asset_ratio = ratio(len(pbr_ready_assets), max(1, len(runtime_assets)))
    lod_ready_asset_ratio = ratio(len(lod_ready_assets), max(1, len(runtime_assets)))
    collision_ready_asset_ratio = ratio(len(collision_ready_assets), max(1, len(runtime_assets)))
    provenance_ready_asset_ratio = ratio(len(provenance_ready_assets), max(1, len(runtime_assets)))

    if required_role_coverage < minimums["required_role_coverage"]:
        blockers.append("required role coverage below minimum")
    if primitive_hero_roles:
        blockers.append("hero role still primitive/blockout")
    if len(runtime_assets) < minimums["min_runtime_mesh_instances"]:
        blockers.append("runtime mesh instance count below minimum")
    if len(unique_runtime_assets) < minimums["min_unique_runtime_assets"]:
        blockers.append("unique runtime asset count below minimum")
    if pbr_texture_asset_ratio < minimums["pbr_texture_asset_ratio"]:
        blockers.append("pbr texture ratio below minimum")
    if lod_ready_asset_ratio < minimums["lod_ready_asset_ratio"]:
        blockers.append("lod readiness ratio below minimum")
    if collision_ready_asset_ratio < minimums["collision_ready_asset_ratio"]:
        blockers.append("collision readiness ratio below minimum")
    if provenance_ready_asset_ratio < minimums["provenance_ready_asset_ratio"]:
        blockers.append("provenance readiness ratio below minimum")
    if primitive_ratio > minimums["primitive_ratio_max"]:
        warnings.append("non-allowlisted primitive ratio above target")

    weighted = {
        "hero_runtime_mesh_ratio": hero_runtime_ratio,
        "all_runtime_mesh_ratio": clamp01(runtime_mesh_ratio / minimums["all_runtime_mesh_ratio"]),
        "primitive_ratio": clamp01(1.0 - ratio(max(0.0, primitive_ratio - minimums["primitive_ratio_max"]), 1.0 - minimums["primitive_ratio_max"])),
        "required_role_coverage": required_role_coverage,
        "pbr_texture_asset_ratio": pbr_texture_asset_ratio,
        "lod_ready_asset_ratio": lod_ready_asset_ratio,
        "collision_ready_asset_ratio": collision_ready_asset_ratio,
        "provenance_ready_asset_ratio": provenance_ready_asset_ratio,
        "unique_runtime_asset_ratio": unique_runtime_asset_ratio,
        "material_preset_ratio": material_preset_ratio,
        "detail_object_ratio": detail_object_ratio,
    }
    score = sum(weighted[key] * float(weights[key]) for key in weights)
    if score < minimums["scene_score"]:
        warnings.append("scene score below AAA minimum")

    return {
        "id": target.get("id", ""),
        "renderer_family": renderer_family,
        "seed": seed_path_raw,
        "status": "PASS" if not blockers and score >= minimums["scene_score"] else "BLOCKED",
        "score": round(score, 4),
        "blockers": sorted(set(blockers)),
        "warnings": sorted(set(warnings)),
        "metrics": {
            "object_count": object_count,
            "runtime_mesh_instances": len(runtime_assets),
            "unique_runtime_assets": len(unique_runtime_assets),
            "primitive_count": len(primitive_objects),
            "non_allowlisted_primitive_count": len(non_allowlisted_primitives),
            "material_preset_count": material_count,
            "hero_object_count": len(hero_objects),
            "hero_runtime_mesh_ratio": round(hero_runtime_ratio, 4),
            "all_runtime_mesh_ratio": round(runtime_mesh_ratio, 4),
            "primitive_ratio": round(primitive_ratio, 4),
            "required_role_coverage": round(required_role_coverage, 4),
            "pbr_texture_asset_ratio": round(pbr_texture_asset_ratio, 4),
            "lod_ready_asset_ratio": round(lod_ready_asset_ratio, 4),
            "collision_ready_asset_ratio": round(collision_ready_asset_ratio, 4),
            "provenance_ready_asset_ratio": round(provenance_ready_asset_ratio, 4),
        },
        "missing_required_roles": missing_required_roles,
        "primitive_hero_roles": primitive_hero_roles,
        "renderer_evidence": renderer or {},
        "asset_registry_coverage": round(ratio(sum(1 for asset in runtime_assets if asset in registry_by_path), max(1, len(runtime_assets))), 4),
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    lines: list[str] = []
    lines.append("# AAA Asset Quality Report")
    lines.append("")
    lines.append(f"Status: `{report['status']}`")
    lines.append(f"Scene count: `{report['scene_count']}`")
    lines.append(f"Passed scenes: `{report['passed_scene_count']}`")
    lines.append(f"Blocked scenes: `{report['blocked_scene_count']}`")
    lines.append("")
    lines.append("## Summary")
    lines.append("")
    lines.append("| Scene | Status | Score | Runtime Meshes | Unique Assets | Primitive Ratio | PBR Ratio | LOD Ratio | Collision Ratio |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|---:|")
    for scene in report["scenes"]:
        metrics = scene["metrics"]
        lines.append(
            f"| {scene['id']} | {scene['status']} | {scene['score']:.4f} | "
            f"{metrics['runtime_mesh_instances']} | {metrics['unique_runtime_assets']} | "
            f"{metrics['primitive_ratio']:.4f} | {metrics['pbr_texture_asset_ratio']:.4f} | "
            f"{metrics['lod_ready_asset_ratio']:.4f} | {metrics['collision_ready_asset_ratio']:.4f} |"
        )
    lines.append("")
    lines.append("## Blockers")
    lines.append("")
    for scene in report["scenes"]:
        lines.append(f"### {scene['id']}")
        if scene["blockers"]:
            for blocker in scene["blockers"]:
                lines.append(f"- {blocker}")
        else:
            lines.append("- none")
        if scene["primitive_hero_roles"]:
            lines.append(f"- primitive hero roles: {', '.join(scene['primitive_hero_roles'])}")
        if scene["missing_required_roles"]:
            lines.append(f"- missing required roles: {', '.join(scene['missing_required_roles'])}")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--import-manifest", type=Path, default=DEFAULT_IMPORT_MANIFEST)
    parser.add_argument("--asset-registry", type=Path, default=DEFAULT_ASSET_REGISTRY)
    parser.add_argument("--renderer-manifest", type=Path, default=None)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    parser.add_argument("--fail-on-blocker", action="store_true")
    args = parser.parse_args()

    contract = load_json(args.contract)
    catalog = load_json(args.catalog)
    import_manifest = load_json(args.import_manifest) if args.import_manifest.exists() else {"assets": []}
    asset_registry = load_json(args.asset_registry) if args.asset_registry.exists() else None
    renderer_manifest = load_json(args.renderer_manifest) if args.renderer_manifest and args.renderer_manifest.exists() else None
    catalog_by_id = scene_catalog_index(catalog)
    renderer_by_family = renderer_family_evidence(renderer_manifest)
    registry_by_path = asset_registry_index(asset_registry)

    scenes = [
        analyze_scene(target, contract, catalog_by_id, renderer_by_family, registry_by_path)
        for target in contract["target_scenes"]
    ]
    blocked = [scene for scene in scenes if scene["status"] != "PASS"]
    report = {
        "schema": contract["report_schema"],
        "status": "PASS" if not blocked else "BLOCKED",
        "contract": rel(args.contract),
        "catalog": rel(args.catalog),
        "import_manifest": rel(args.import_manifest),
        "asset_registry": rel(args.asset_registry) if args.asset_registry.exists() else "",
        "renderer_manifest": rel(args.renderer_manifest) if args.renderer_manifest else "",
        "scene_count": len(scenes),
        "passed_scene_count": len(scenes) - len(blocked),
        "blocked_scene_count": len(blocked),
        "minimums": contract["minimums"],
        "scenes": scenes,
    }
    args.out_json.parent.mkdir(parents=True, exist_ok=True)
    with args.out_json.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(report, indent=2) + "\n")
    write_markdown(report, args.out_md)
    print(json.dumps({"status": report["status"], "report": rel(args.out_json), "markdown": rel(args.out_md)}, indent=2))
    return 1 if args.fail_on_blocker and blocked else 0


if __name__ == "__main__":
    raise SystemExit(main())
