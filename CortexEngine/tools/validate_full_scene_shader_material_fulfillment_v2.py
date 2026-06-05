#!/usr/bin/env python3
"""Validate V2 material provider fulfillment/admission records."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA = ROOT / "assets/final_art/full_scene_shader_material_fulfillment_v2.schema.json"
DEFAULT_PROVIDER_MANIFEST = (
    ROOT / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_requests/manifest.json"
)
DEFAULT_FULFILLMENT_MANIFEST = (
    ROOT
    / "docs/media/final_art/generated/full_scene_shader_pipeline_v2/provider_fulfillment/fulfillment_manifest.json"
)


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def fail(message: str) -> int:
    print(f"FAIL: {message}", file=sys.stderr)
    return 1


def has_required_admission(admission: dict[str, Any], required: list[str]) -> list[str]:
    missing: list[str] = []
    for field in required:
        value = admission.get(field)
        if value in ("", None, [], {}):
            missing.append(field)
    return missing


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--provider-manifest", type=Path, default=DEFAULT_PROVIDER_MANIFEST)
    parser.add_argument("--fulfillment-manifest", type=Path, default=DEFAULT_FULFILLMENT_MANIFEST)
    parser.add_argument("--fail-on-pending", action="store_true")
    args = parser.parse_args()

    errors: list[str] = []
    schema = load_json(args.schema)
    provider_manifest = load_json(args.provider_manifest)
    fulfillment = load_json(args.fulfillment_manifest)

    for field in schema.get("required_manifest_fields", []):
        if field not in fulfillment:
            errors.append(f"fulfillment manifest missing {field}")
    if fulfillment.get("schema") != "cortex.full_scene_shader_material_fulfillment_manifest.v2":
        errors.append("fulfillment manifest schema id is invalid")

    provider_ids = {request.get("id") for request in provider_manifest.get("requests", [])}
    fulfillment_requests = fulfillment.get("requests", [])
    fulfillment_ids = {request.get("request_id") for request in fulfillment_requests}
    missing = sorted(request_id for request_id in provider_ids - fulfillment_ids if request_id)
    extra = sorted(request_id for request_id in fulfillment_ids - provider_ids if request_id)
    if missing:
        errors.append("fulfillment missing provider requests: " + ", ".join(missing[:20]))
    if extra:
        errors.append("fulfillment has unknown requests: " + ", ".join(extra[:20]))

    allowed_statuses = set(schema.get("statuses", []))
    required_request_fields = schema.get("required_request_fields", [])
    required_admitted_fields = schema.get("required_admitted_fields", [])
    pending_count = 0
    admitted_count = 0
    for request in fulfillment_requests:
        request_id = request.get("request_id", "<missing>")
        for field in required_request_fields:
            if field not in request:
                errors.append(f"{request_id} missing {field}")
        status = request.get("status")
        if status not in allowed_statuses:
            errors.append(f"{request_id} has invalid status {status!r}")
        if status == "PENDING":
            pending_count += 1
        if status == "ADMITTED":
            admitted_count += 1
            admission = request.get("admission")
            if not isinstance(admission, dict):
                errors.append(f"{request_id} admitted without admission object")
            else:
                missing_admission = has_required_admission(admission, required_admitted_fields)
                if missing_admission:
                    errors.append(
                        f"{request_id} admitted but missing admission fields: "
                        + ", ".join(missing_admission)
                    )
        if status in {"FULFILLED", "ADMITTED"}:
            package = request.get("submitted_package", {})
            if not package.get("package_root") or not package.get("manifest"):
                errors.append(f"{request_id} fulfilled/admitted without package_root and manifest")

    if args.fail_on_pending and pending_count:
        errors.append(f"pending fulfillments remain: {pending_count}")

    summary = fulfillment.get("summary", {})
    if summary.get("request_count") != len(provider_ids):
        errors.append("summary request_count does not match provider manifest")
    if summary.get("pending_count") != pending_count:
        errors.append("summary pending_count does not match request statuses")
    if summary.get("admitted_count") != admitted_count:
        errors.append("summary admitted_count does not match request statuses")

    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    print("PASS: Full Scene Shader Pipeline V2 material fulfillment manifest is coherent")
    print(f"Fulfillment status: {fulfillment.get('status')}")
    print(f"Requests: {len(fulfillment_requests)}")
    print(f"Pending: {pending_count}")
    print(f"Admitted: {admitted_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
