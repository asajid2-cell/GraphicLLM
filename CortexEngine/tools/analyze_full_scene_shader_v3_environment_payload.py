#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_PROXY_DERIVATION = "profile_payload_inventory_v1"
PROXY_MANIFEST_PATH = ROOT / "assets" / "textures" / "scene_local_proxy" / "proxy_manifest.json"


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def load_proxy_manifest() -> dict[str, Any]:
    if PROXY_MANIFEST_PATH.exists():
        return load_json(PROXY_MANIFEST_PATH)
    return {}


def report_paths(manifest: dict[str, Any]) -> list[Path]:
    paths: list[Path] = []
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        report = row.get("report")
        if isinstance(report, str) and report:
            paths.append(Path(report))
    return paths


def analyze_report(path: Path, proxy_manifest: dict[str, Any]) -> dict[str, Any]:
    report = load_json(path)
    contract = report.get("frame_contract", {})
    environment = contract.get("environment", {})
    v3 = contract.get("full_scene_shader_pipeline_v3", {})
    scene_visual = contract.get("scene_visual_contract", {})
    policy_contract = v3.get("scene_profile_policy_contract", {})
    if not isinstance(policy_contract, dict):
        policy_contract = {}
    texture_set_id = environment.get("scene_local_texture_set_id", "none")
    proxy_sets = proxy_manifest.get("sets", {})
    proxy_manifest_set = proxy_sets.get(texture_set_id, {}) if isinstance(proxy_sets, dict) else {}
    proxy_derivation = proxy_manifest_set.get("derivation", {}) if isinstance(proxy_manifest_set, dict) else {}
    if not isinstance(proxy_derivation, dict):
        proxy_derivation = {}
    row = {
        "report": str(path),
        "family": scene_visual.get("family", "unknown"),
        "profile_id": scene_visual.get("profile_id", "unknown"),
        "environment_ready": v3.get("scene_local_environment_ready") is True,
        "profile_policy_consumed": v3.get("scene_local_environment_consumes_scene_profile_policy") is True,
        "profile_policy_contract_id": v3.get("scene_local_environment_profile_contract_id", "unknown"),
        "profile_policy_environment": v3.get("scene_local_environment_profile_policy", "unknown"),
        "profile_policy_enclosure_mode": v3.get("scene_local_environment_profile_enclosure_mode", "unknown"),
        "profile_policy_reflection": v3.get("scene_local_environment_profile_reflection_policy", "unknown"),
        "shader_profile": v3.get("scene_local_environment_shader_profile", "unknown"),
        "shader_profile_mode": float(v3.get("scene_local_environment_shader_profile_mode", -1.0) or -1.0),
        "local_background_strength": float(
            v3.get("scene_local_environment_local_background_strength", -1.0) or -1.0
        ),
        "scene_profile_policy_contract_id": policy_contract.get("contract_id", "unknown"),
        "scene_profile_policy_environment": policy_contract.get("environment_policy", "unknown"),
        "scene_profile_policy_enclosure_mode": policy_contract.get("enclosure_mode", "unknown"),
        "scene_profile_policy_reflection": policy_contract.get("reflection_policy", "unknown"),
        "texture_set_id": texture_set_id,
        "texture_set_path": environment.get("scene_local_texture_set_path", ""),
        "texture_set_present": environment.get("scene_local_texture_set_present") is True,
        "texture_count": int(environment.get("scene_local_texture_count", 0) or 0),
        "albedo_count": int(environment.get("scene_local_albedo_texture_count", 0) or 0),
        "normal_count": int(environment.get("scene_local_normal_texture_count", 0) or 0),
        "payload_ready": environment.get("scene_local_payload_ready") is True,
        "irradiance_proxy_ready": environment.get("scene_local_irradiance_proxy_ready") is True,
        "specular_proxy_ready": environment.get("scene_local_specular_proxy_ready") is True,
        "visible_background_proxy_ready": environment.get("scene_local_visible_background_proxy_ready") is True,
        "v3_payload_ready": v3.get("scene_local_texture_payload_ready") is True,
        "v3_payload_count": int(v3.get("scene_local_texture_payload_count", 0) or 0),
        "v3_texture_set_id": v3.get("scene_local_texture_set_id", "none"),
        "texture_richness": float(environment.get("scene_local_payload_texture_richness", -1.0) or -1.0),
        "proxy_score": float(environment.get("scene_local_payload_proxy_score", -1.0) or -1.0),
        "shader_influence": float(environment.get("scene_local_payload_shader_influence", -1.0) or -1.0),
        "v3_texture_richness": float(v3.get("scene_local_texture_payload_richness", -1.0) or -1.0),
        "v3_proxy_score": float(v3.get("scene_local_texture_payload_proxy_score", -1.0) or -1.0),
        "v3_shader_influence": float(v3.get("scene_local_texture_payload_shader_influence", -1.0) or -1.0),
        "resource_table_required": environment.get("scene_local_payload_resource_table_required") is True,
        "resource_table_bindable": environment.get("scene_local_payload_resource_table_bindable") is True,
        "bound_resource_count": int(environment.get("scene_local_payload_bound_resource_count", 0) or 0),
        "binding_source": environment.get("scene_local_payload_binding_source", "none"),
        "fallback_reason": environment.get("scene_local_payload_fallback_reason", "none"),
        "v3_resource_table_required": v3.get("scene_local_texture_payload_resource_table_required") is True,
        "v3_resource_table_bindable": v3.get("scene_local_texture_payload_resource_table_bindable") is True,
        "v3_bound_resource_count": int(v3.get("scene_local_texture_payload_bound_resource_count", 0) or 0),
        "v3_binding_source": v3.get("scene_local_texture_payload_binding_source", "none"),
        "v3_fallback_reason": v3.get("scene_local_texture_payload_fallback_reason", "none"),
        "proxy_resource_table_required": environment.get("scene_local_proxy_resource_table_required") is True,
        "proxy_resource_table_bindable": environment.get("scene_local_proxy_resource_table_bindable") is True,
        "bound_proxy_resource_count": int(environment.get("scene_local_proxy_bound_resource_count", 0) or 0),
        "proxy_binding_source": environment.get("scene_local_proxy_binding_source", "none"),
        "proxy_fallback_reason": environment.get("scene_local_proxy_fallback_reason", "none"),
        "v3_proxy_resource_table_required": (
            v3.get("scene_local_environment_proxy_resource_table_required") is True
        ),
        "v3_proxy_resource_table_bindable": (
            v3.get("scene_local_environment_proxy_resource_table_bindable") is True
        ),
        "v3_bound_proxy_resource_count": int(
            v3.get("scene_local_environment_proxy_bound_resource_count", 0) or 0
        ),
        "v3_proxy_binding_source": v3.get("scene_local_environment_proxy_binding_source", "none"),
        "v3_proxy_fallback_reason": v3.get("scene_local_environment_proxy_fallback_reason", "none"),
        "proxy_manifest_present": bool(proxy_manifest_set),
        "proxy_derivation_method": proxy_derivation.get("method", "none"),
        "failures": [],
    }
    if row["environment_ready"]:
        if not row["profile_policy_consumed"]:
            row["failures"].append("environment ready without consuming SceneProfileV3 policy")
        if row["profile_policy_contract_id"] != row["scene_profile_policy_contract_id"]:
            row["failures"].append("environment profile contract id does not match SceneProfileV3 policy")
        if row["profile_policy_environment"] != row["scene_profile_policy_environment"]:
            row["failures"].append("environment policy does not match SceneProfileV3 policy")
        if row["profile_policy_enclosure_mode"] != row["scene_profile_policy_enclosure_mode"]:
            row["failures"].append("environment enclosure mode does not match SceneProfileV3 policy")
        if row["profile_policy_reflection"] != row["scene_profile_policy_reflection"]:
            row["failures"].append("environment reflection policy does not match SceneProfileV3 policy")
        if str(row["shader_profile"]).strip().lower() in {"", "unknown", "none", "default"}:
            row["failures"].append("environment ready without shader profile")
        if row["shader_profile_mode"] < 0.0 or row["shader_profile_mode"] > 4.0:
            row["failures"].append("environment shader profile mode out of range")
        if row["local_background_strength"] < 0.0 or row["local_background_strength"] > 1.0:
            row["failures"].append("environment local background strength out of range")
    if row["payload_ready"]:
        if row["texture_count"] < 2:
            row["failures"].append("payload ready with fewer than two textures")
        if row["albedo_count"] <= 0:
            row["failures"].append("payload ready without albedo texture")
        if row["normal_count"] <= 0:
            row["failures"].append("payload ready without normal texture")
        if not (row["irradiance_proxy_ready"] or row["specular_proxy_ready"] or row["visible_background_proxy_ready"]):
            row["failures"].append("payload ready without any usable proxy")
        if not row["v3_payload_ready"]:
            row["failures"].append("environment payload ready but V3 payload flag is false")
        if row["v3_payload_count"] != row["texture_count"]:
            row["failures"].append("V3 payload count does not match environment texture count")
        if row["v3_texture_set_id"] != row["texture_set_id"]:
            row["failures"].append("V3 texture set id does not match environment texture set id")
        if row["texture_richness"] <= 0.0 or row["texture_richness"] > 1.0:
            row["failures"].append("payload ready with invalid texture richness")
        if row["proxy_score"] <= 0.0 or row["proxy_score"] > 1.0:
            row["failures"].append("payload ready with invalid proxy score")
        if row["shader_influence"] <= 0.0 or row["shader_influence"] > 1.0:
            row["failures"].append("payload ready with invalid shader influence")
        if abs(row["v3_texture_richness"] - row["texture_richness"]) > 0.001:
            row["failures"].append("V3 texture richness does not match environment value")
        if abs(row["v3_proxy_score"] - row["proxy_score"]) > 0.001:
            row["failures"].append("V3 proxy score does not match environment value")
        if abs(row["v3_shader_influence"] - row["shader_influence"]) > 0.001:
            row["failures"].append("V3 shader influence does not match environment value")
        if not row["resource_table_required"]:
            row["failures"].append("payload ready without resource table requirement")
        if not row["resource_table_bindable"]:
            row["failures"].append("payload ready without bindable shader resource table")
        if row["bound_resource_count"] <= 0:
            row["failures"].append("payload ready without bound payload resources")
        if row["v3_resource_table_required"] != row["resource_table_required"]:
            row["failures"].append("V3 resource-table-required flag does not match environment value")
        if row["v3_resource_table_bindable"] != row["resource_table_bindable"]:
            row["failures"].append("V3 resource-table-bindable flag does not match environment value")
        if row["v3_bound_resource_count"] != row["bound_resource_count"]:
            row["failures"].append("V3 bound resource count does not match environment value")
        if row["v3_binding_source"] != row["binding_source"]:
            row["failures"].append("V3 binding source does not match environment value")
        if row["v3_fallback_reason"] != row["fallback_reason"]:
            row["failures"].append("V3 fallback reason does not match environment value")
        if not row["proxy_resource_table_required"]:
            row["failures"].append("payload ready without proxy resource table requirement")
        if not row["proxy_resource_table_bindable"]:
            row["failures"].append("payload ready without bindable proxy resource table")
        if row["bound_proxy_resource_count"] <= 0:
            row["failures"].append("payload ready without bound scene-local proxy resources")
        if row["v3_proxy_resource_table_required"] != row["proxy_resource_table_required"]:
            row["failures"].append("V3 proxy-resource-table-required flag does not match environment value")
        if row["v3_proxy_resource_table_bindable"] != row["proxy_resource_table_bindable"]:
            row["failures"].append("V3 proxy-resource-table-bindable flag does not match environment value")
        if row["v3_bound_proxy_resource_count"] != row["bound_proxy_resource_count"]:
            row["failures"].append("V3 bound proxy resource count does not match environment value")
        if row["v3_proxy_binding_source"] != row["proxy_binding_source"]:
            row["failures"].append("V3 proxy binding source does not match environment value")
        if row["v3_proxy_fallback_reason"] != row["proxy_fallback_reason"]:
            row["failures"].append("V3 proxy fallback reason does not match environment value")
        if row["proxy_binding_source"] != "cached_explicit_scene_local_proxy_triple":
            row["failures"].append(
                f"payload ready without explicit generated/authored proxy binding: {row['proxy_binding_source']}"
            )
        if not row["proxy_manifest_present"]:
            row["failures"].append("payload ready without scene-local proxy manifest entry")
        if row["proxy_derivation_method"] != EXPECTED_PROXY_DERIVATION:
            row["failures"].append(
                "payload ready without current derived scene-local proxy assets: "
                f"{row['proxy_derivation_method']}"
            )
    return row


def write_markdown(path: Path, result: dict[str, Any]) -> None:
    lines = [
        "# V3 Scene-Local Environment Payload",
        "",
        f"- manifest: `{result['manifest']}`",
        f"- report count: `{result['report_count']}`",
        f"- texture-set-present reports: `{result['texture_set_present_report_count']}`",
        f"- payload-ready reports: `{result['payload_ready_report_count']}`",
        f"- profile-policy-consumed reports: `{result['profile_policy_consumed_report_count']}`",
        f"- failures: `{len(result['failures'])}`",
        "",
        "| Family | Profile Policy | Shader Profile | Local Background | Texture Set | Textures | Albedo | Normal | Payload | Influence | Bound | Proxy Bound | Binding | Proxy Binding | Derivation | Proxies |",
        "|---|---|---|---:|---|---:|---:|---:|---|---:|---:|---:|---|---|---|---|",
    ]
    for row in result["rows"]:
        proxies = ",".join(
            name
            for name, ready in [
                ("irradiance", row["irradiance_proxy_ready"]),
                ("specular", row["specular_proxy_ready"]),
                ("visible", row["visible_background_proxy_ready"]),
            ]
            if ready
        ) or "none"
        lines.append(
            "| "
            + " | ".join(
                [
                    str(row["family"]),
                    f"`{row['profile_policy_environment']}`",
                    f"`{row['shader_profile']}`",
                    f"{row['local_background_strength']:.2f}",
                    f"`{row['texture_set_id']}`",
                    str(row["texture_count"]),
                    str(row["albedo_count"]),
                    str(row["normal_count"]),
                    str(row["payload_ready"]).lower(),
                    f"{row['shader_influence']:.2f}",
                    str(row["bound_resource_count"]),
                    str(row["bound_proxy_resource_count"]),
                    f"`{row['binding_source']}`",
                    f"`{row['proxy_binding_source']}`",
                    f"`{row['proxy_derivation_method']}`",
                    proxies,
                ]
            )
            + " |"
        )
    if result["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in result["failures"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    parser.add_argument("--min-payload-ready", type=int, default=0)
    args = parser.parse_args()

    manifest = load_json(args.manifest)
    proxy_manifest = load_proxy_manifest()
    rows = [analyze_report(path, proxy_manifest) for path in report_paths(manifest)]
    failures: list[str] = []
    for row in rows:
        failures.extend(f"{row['report']}: {failure}" for failure in row["failures"])
    payload_ready_count = sum(1 for row in rows if row["payload_ready"])
    if payload_ready_count < args.min_payload_ready:
        failures.append(
            f"payload-ready report count {payload_ready_count} is below required {args.min_payload_ready}"
        )

    result = {
        "schema": "cortex.full_scene_shader_pipeline_v3.environment_payload.v1",
        "manifest": str(args.manifest),
        "report_count": len(rows),
        "texture_set_present_report_count": sum(1 for row in rows if row["texture_set_present"]),
        "payload_ready_report_count": payload_ready_count,
        "shader_influence_report_count": sum(1 for row in rows if row["shader_influence"] > 0.0),
        "resource_bindable_report_count": sum(1 for row in rows if row["resource_table_bindable"]),
        "bound_resource_report_count": sum(1 for row in rows if row["bound_resource_count"] > 0),
        "proxy_resource_bindable_report_count": sum(1 for row in rows if row["proxy_resource_table_bindable"]),
        "bound_proxy_resource_report_count": sum(1 for row in rows if row["bound_proxy_resource_count"] > 0),
        "explicit_proxy_binding_report_count": sum(
            1 for row in rows if row["proxy_binding_source"] == "cached_explicit_scene_local_proxy_triple"
        ),
        "derived_proxy_report_count": sum(
            1 for row in rows if row["proxy_derivation_method"] == EXPECTED_PROXY_DERIVATION
        ),
        "proxy_manifest": str(PROXY_MANIFEST_PATH),
        "profile_policy_consumed_report_count": sum(1 for row in rows if row["profile_policy_consumed"]),
        "rows": rows,
        "failures": failures,
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.output_md, result)
    if failures:
        print(f"FAIL: V3 environment payload diagnostics found {len(failures)} failure(s)")
        print(f"json={args.output_json}")
        print(f"markdown={args.output_md}")
        return 1
    print("PASS: V3 environment payload diagnostics are measurable")
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
