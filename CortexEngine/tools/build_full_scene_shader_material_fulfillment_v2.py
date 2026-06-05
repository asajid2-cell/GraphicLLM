#!/usr/bin/env python3
"""Create a pending V2 material fulfillment manifest from provider requests."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PROVIDER_MANIFEST = (
    ROOT / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.json"
)
DEFAULT_OUT_JSON = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.json"
)
DEFAULT_OUT_MD = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.md"
)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def pending_request(request: dict[str, Any]) -> dict[str, Any]:
    material_contract = request.get("material_contract", {})
    return {
        "request_id": request.get("id", ""),
        "priority": request.get("priority", ""),
        "kind": request.get("kind", ""),
        "request_path": request.get("request_path", ""),
        "status": "PENDING",
        "expected_outputs": {
            "accepted_outputs": request.get("accepted_outputs", []),
            "required_pbr_maps": material_contract.get("required_pbr_maps", []),
            "shader_feature_flags": material_contract.get("shader_feature_flags", []),
            "material_families": material_contract.get("material_families", []),
            "texture_resolution_min": material_contract.get("texture_resolution_min", 0),
            "texture_resolution_target": material_contract.get("texture_resolution_target", 0),
        },
        "submitted_package": {
            "provider": "",
            "package_root": "",
            "manifest": "",
            "notes": ""
        },
        "admission": {
            "runtime_asset": "",
            "asset_registry_patch": "",
            "pbr_maps": {},
            "lod_chain": [],
            "collision_proxy": "",
            "preview": "",
            "provenance": {},
            "v2_material_evidence_delta": "",
            "renderer_v1_packet": "",
            "v2_material_debug_packet": "",
            "admission_notes": "pending provider/library fulfillment"
        }
    }


def markdown(manifest: dict[str, Any]) -> str:
    lines = [
        "# Full Scene Shader Pipeline V2 Material Fulfillment",
        "",
        f"Status: `{manifest['status']}`",
        "",
        "## Summary",
        "",
    ]
    for key, value in manifest["summary"].items():
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(["", "## Pending P0 Requests", ""])
    for request in manifest["requests"]:
        if request["priority"] != "P0" or request["status"] != "PENDING":
            continue
        lines.append(f"- `{request['request_id']}` ({request['kind']})")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--provider-manifest", type=Path, default=DEFAULT_PROVIDER_MANIFEST)
    parser.add_argument("--out-json", type=Path, default=DEFAULT_OUT_JSON)
    parser.add_argument("--out-md", type=Path, default=DEFAULT_OUT_MD)
    args = parser.parse_args()

    provider_manifest = load_json(args.provider_manifest)
    requests = [pending_request(request) for request in provider_manifest.get("requests", [])]
    status_counts = Counter(request["status"] for request in requests)
    priority_counts = Counter(request["priority"] for request in requests)
    manifest = {
        "schema": "cortex.full_scene_shader_material_fulfillment_manifest.v2",
        "generated_by": "tools/build_full_scene_shader_material_fulfillment_v2.py",
        "source_provider_manifest": args.provider_manifest.relative_to(ROOT).as_posix(),
        "status": "PENDING" if requests else "EMPTY",
        "summary": {
            "request_count": len(requests),
            "pending_count": status_counts.get("PENDING", 0),
            "submitted_count": status_counts.get("SUBMITTED", 0),
            "fulfilled_count": status_counts.get("FULFILLED", 0),
            "admitted_count": status_counts.get("ADMITTED", 0),
            "rejected_count": status_counts.get("REJECTED", 0),
            "p0_count": priority_counts.get("P0", 0),
            "p1_count": priority_counts.get("P1", 0)
        },
        "requests": requests
    }
    write_json(args.out_json, manifest)
    write_text(args.out_md, markdown(manifest))
    print(
        json.dumps(
            {
                "status": manifest["status"],
                "request_count": len(requests),
                "pending_count": manifest["summary"]["pending_count"],
                "manifest": args.out_json.relative_to(ROOT).as_posix(),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
