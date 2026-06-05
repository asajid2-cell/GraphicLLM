#!/usr/bin/env python3
"""Build a central registry for runtime mesh assets used by final-art scenes."""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONTRACT = ROOT / "assets/final_art/aaa_asset_quality_contract.json"
DEFAULT_IMPORT_MANIFEST = ROOT / "assets/generated/pretrained_assets/import_manifest.json"
DEFAULT_OUT = ROOT / "assets/final_art/asset_registry_v2.json"


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def normalize(path: str) -> str:
    return path.replace("\\", "/").strip()


def asset_id(path: str) -> str:
    stem = normalize(path).rsplit("/", 1)[-1].rsplit(".", 1)[0]
    return re.sub(r"[^a-zA-Z0-9_]+", "_", stem).strip("_").lower()


def is_runtime_asset(path: str) -> bool:
    return normalize(path).lower().endswith((".gltf", ".glb"))


def source_class(path: str) -> str:
    normalized = normalize(path)
    lowered = normalized.lower()
    if "assets/models/naturalistic_showcase/" in normalized:
        return "artist_authored_pbr"
    if "assets/models/kenney_furniture_kit/" in normalized:
        return "cc0_curated_library"
    if "assets/generated/final_art_fidelity_meshes/" in normalized:
        return "engine_generated_fidelity_mesh"
    if "openai_shap_e_text300m" in lowered or "shap_e" in lowered:
        return "prototype_pretrained_generated"
    if "assets/generated/pretrained_assets/" in normalized:
        return "prototype_pretrained_generated"
    return "unknown_runtime_asset"


def provenance_for(path: str, imported_by_path: dict[str, dict[str, Any]]) -> dict[str, Any]:
    normalized = normalize(path)
    imported = imported_by_path.get(normalized) or imported_by_path.get(normalized.replace("assets/generated/pretrained_assets/", ""))
    cls = source_class(normalized)
    if imported:
        runtime = imported.get("runtime_asset", {})
        return {
            "provider": imported.get("provider", ""),
            "source_model": imported.get("source_model", ""),
            "license_policy": imported.get("license_policy", ""),
            "triangle_count": runtime.get("triangle_count", 0),
            "texture_megabytes": runtime.get("texture_megabytes", 0.0),
            "preview_image": imported.get("preview_image", ""),
            "visual_preview_score": (imported.get("admission") or {}).get("visual_preview_score", 0.0),
        }
    if cls == "artist_authored_pbr":
        return {
            "provider": "repo_artist_authored",
            "source_model": "",
            "license_policy": "project_approved_runtime_asset",
            "triangle_count": 0,
            "texture_megabytes": 0.0,
            "preview_image": "",
            "visual_preview_score": 0.0,
        }
    if cls == "cc0_curated_library":
        return {
            "provider": "kenney_cc0_furniture_kit",
            "source_model": "",
            "license_policy": "cc0_reference_compatible",
            "triangle_count": 0,
            "texture_megabytes": 0.0,
            "preview_image": "",
            "visual_preview_score": 0.0,
        }
    if cls == "engine_generated_fidelity_mesh":
        return {
            "provider": "cortex_engine_generated_mesh",
            "source_model": "tools/generate_final_art_fidelity_meshes.py",
            "license_policy": "project_authored_generated_asset",
            "triangle_count": 0,
            "texture_megabytes": 0.0,
            "preview_image": "",
            "visual_preview_score": 0.0,
        }
    return {
        "provider": "",
        "source_model": "",
        "license_policy": "",
        "triangle_count": 0,
        "texture_megabytes": 0.0,
        "preview_image": "",
        "visual_preview_score": 0.0,
    }


def readiness(path: str, provenance: dict[str, Any]) -> dict[str, bool]:
    cls = source_class(path)
    editable = cls != "unknown_runtime_asset"
    separated = cls != "unknown_runtime_asset"
    provenance_ready = bool(provenance.get("license_policy"))
    pbr_complete = cls in {"artist_authored_pbr"}
    if cls == "prototype_pretrained_generated":
        pbr_complete = False
    lod_ready = False
    collision_ready = False
    scale_ready = cls != "unknown_runtime_asset"
    support_anchor_ready = cls in {"artist_authored_pbr", "cc0_curated_library", "engine_generated_fidelity_mesh"}
    preview_ready = bool(provenance.get("preview_image")) or cls in {"artist_authored_pbr", "cc0_curated_library"}
    aaa_ready = all(
        [
            editable,
            separated,
            provenance_ready,
            pbr_complete,
            lod_ready,
            collision_ready,
            scale_ready,
            support_anchor_ready,
            preview_ready,
        ]
    )
    return {
        "editable_mesh": editable,
        "separated_object": separated,
        "provenance_ready": provenance_ready,
        "pbr_textures_complete": pbr_complete,
        "lod_chain_ready": lod_ready,
        "collision_proxy_ready": collision_ready,
        "scale_bounds_known": scale_ready,
        "support_anchor_known": support_anchor_ready,
        "preview_ready": preview_ready,
        "aaa_ready": aaa_ready,
    }


def blockers(readiness_flags: dict[str, bool], path: str) -> list[str]:
    labels = {
        "editable_mesh": "not editable mesh",
        "separated_object": "not separated object",
        "provenance_ready": "missing provenance/license",
        "pbr_textures_complete": "missing complete PBR texture set",
        "lod_chain_ready": "missing LOD chain",
        "collision_proxy_ready": "missing collision proxy",
        "scale_bounds_known": "missing scale/bounds contract",
        "support_anchor_known": "missing support/contact anchors",
        "preview_ready": "missing preview evidence",
    }
    out = [label for key, label in labels.items() if not readiness_flags.get(key, False)]
    if source_class(path) == "engine_generated_fidelity_mesh":
        out.append("engine proxy mesh; useful detail but not AAA final asset")
    if source_class(path) == "prototype_pretrained_generated":
        out.append("prototype generated asset; needs higher-quality provider or human approval")
    return out


def imported_index(import_manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    by_path: dict[str, dict[str, Any]] = {}
    for asset in import_manifest.get("assets", []):
        runtime_path = normalize(str((asset.get("runtime_asset") or {}).get("path", "")))
        if runtime_path:
            by_path[runtime_path] = asset
            by_path[f"assets/generated/pretrained_assets/{runtime_path}"] = asset
    return by_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--contract", type=Path, default=DEFAULT_CONTRACT)
    parser.add_argument("--import-manifest", type=Path, default=DEFAULT_IMPORT_MANIFEST)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    contract = load_json(args.contract)
    import_manifest = load_json(args.import_manifest) if args.import_manifest.exists() else {"assets": []}
    imported_by_path = imported_index(import_manifest)
    refs: dict[str, dict[str, Any]] = {}
    for target in contract["target_scenes"]:
        seed_path_raw = str(target.get("admitted_seed", ""))
        if not seed_path_raw:
            continue
        seed_path = ROOT / seed_path_raw
        if not seed_path.exists():
            continue
        seed = load_json(seed_path)
        for obj in seed.get("objects", []):
            runtime_path = normalize(str(obj.get("runtime_asset", "")))
            if not is_runtime_asset(runtime_path):
                continue
            entry = refs.setdefault(
                runtime_path,
                {
                    "runtime_asset": runtime_path,
                    "referenced_by": [],
                    "semantic_roles": set(),
                    "scene_families": set(),
                },
            )
            entry["referenced_by"].append(
                {
                    "scene": target["id"],
                    "object": obj.get("id", ""),
                    "role": obj.get("role", ""),
                    "material": obj.get("material", ""),
                }
            )
            entry["semantic_roles"].add(str(obj.get("role", "")))
            entry["scene_families"].add(str(target["id"]))

    assets: list[dict[str, Any]] = []
    source_counts: defaultdict[str, int] = defaultdict(int)
    ready_count = 0
    for runtime_path, entry in sorted(refs.items()):
        cls = source_class(runtime_path)
        prov = provenance_for(runtime_path, imported_by_path)
        ready = readiness(runtime_path, prov)
        if ready["aaa_ready"]:
            ready_count += 1
        source_counts[cls] += 1
        assets.append(
            {
                "id": asset_id(runtime_path),
                "runtime_asset": runtime_path,
                "source_class": cls,
                "provenance": prov,
                "referenced_by": entry["referenced_by"],
                "semantic_roles": sorted(role for role in entry["semantic_roles"] if role),
                "scene_families": sorted(entry["scene_families"]),
                "readiness": ready,
                "quality_blockers": blockers(ready, runtime_path),
            }
        )

    report = {
        "schema": "cortex.asset_registry_v2",
        "generated_by": "tools/build_asset_registry_v2.py",
        "asset_count": len(assets),
        "source_scene_count": len({scene for asset in assets for scene in asset["scene_families"]}),
        "summary": {
            "aaa_ready_asset_count": ready_count,
            "aaa_ready_asset_ratio": 0.0 if not assets else ready_count / len(assets),
            "source_class_counts": dict(sorted(source_counts.items())),
        },
        "assets": assets,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "PASS", "registry": args.out.relative_to(ROOT).as_posix(), "asset_count": len(assets), "aaa_ready_asset_count": ready_count}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
