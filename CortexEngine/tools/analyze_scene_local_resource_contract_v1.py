#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONTRACT = ROOT / "assets" / "final_art" / "scene_local_resource_contract_v1.json"
MATERIAL_PAYLOAD_DEBUG_MODES = {2, 3, 35, 36, 41, 47, 48, 49, 50, 51, 52, 53, 82}


UNKNOWN_STRINGS = {"", "unknown", "none", "default", "unprofiled", "planned"}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def diagnostic_scope(report: dict[str, Any]) -> str:
    renderer = report.get("renderer")
    if isinstance(renderer, dict):
        try:
            debug_view_mode = int(renderer.get("debug_view_mode", -1))
        except (TypeError, ValueError):
            debug_view_mode = -1
        if debug_view_mode in MATERIAL_PAYLOAD_DEBUG_MODES:
            return "material_payload"
    return "full_pipeline"


def known(value: Any) -> bool:
    if not isinstance(value, str):
        return False
    return value.strip().lower() not in UNKNOWN_STRINGS


def report_paths(manifest: dict[str, Any]) -> list[Path]:
    paths: list[Path] = []
    seen: set[str] = set()
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        report = row.get("report")
        if not isinstance(report, str) or not report or report in seen:
            continue
        seen.add(report)
        paths.append(Path(report))
    return paths


def view_filter_set(manifest: dict[str, Any]) -> set[str]:
    value = manifest.get("view_filter", "")
    if not isinstance(value, str):
        return set()
    return {item.strip() for item in value.split(",") if item.strip()}


def domain_by_id(v3: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(domain.get("id", "")): domain
        for domain in v3.get("domains", [])
        if isinstance(domain, dict)
    }


def nested_value(report: dict[str, Any], frame_contract: dict[str, Any], field: str) -> Any:
    if field.startswith("scene_profile_policy_contract."):
        v3 = frame_contract.get("full_scene_shader_pipeline_v3", {})
        policy = v3.get("scene_profile_policy_contract", {}) if isinstance(v3, dict) else {}
        if not isinstance(policy, dict):
            return None
        return policy.get(field.split(".", 1)[1])
    if field.startswith("scene_visual_contract."):
        scene_visual = frame_contract.get("scene_visual_contract", {})
        if not isinstance(scene_visual, dict):
            return None
        return scene_visual.get(field.split(".", 1)[1])
    scene_visual = frame_contract.get("scene_visual_contract", {})
    if isinstance(scene_visual, dict) and field in scene_visual:
        return scene_visual.get(field)
    v3 = frame_contract.get("full_scene_shader_pipeline_v3", {})
    if isinstance(v3, dict) and field in v3:
        return v3.get(field)
    return report.get(field)


def field_is_proved(report: dict[str, Any], frame_contract: dict[str, Any], field: str) -> bool:
    value = nested_value(report, frame_contract, field)
    if isinstance(value, bool):
        if field.endswith("visible_external_hdri_allowed"):
            return True
        if field.endswith("invalid_external_hdri"):
            return value is False
        return value is True
    if isinstance(value, (int, float)):
        return value > 0
    return known(value)


def canonical_family(contract: dict[str, Any], scene_family: str, manifest_family: str) -> str:
    aliases = contract.get("family_aliases", {})
    if not isinstance(aliases, dict):
        aliases = {}
    for candidate in (scene_family, manifest_family):
        if not isinstance(candidate, str) or not candidate:
            continue
        if candidate in contract.get("family_contracts", {}):
            return candidate
        alias = aliases.get(candidate)
        if isinstance(alias, str) and alias:
            return alias
        for family in contract.get("family_contracts", {}):
            if family and family in candidate:
                return str(family)
    return ""


def analyze_report(
    path: Path,
    manifest_family_by_report: dict[str, str],
    contract: dict[str, Any],
    requested_views: set[str],
) -> dict[str, Any]:
    row: dict[str, Any] = {
        "report": str(path),
        "ready": False,
        "failures": [],
        "warnings": [],
    }
    if not path.exists():
        row["failures"].append("missing report")
        return row

    report = load_json(path)
    scope = diagnostic_scope(report)
    frame_contract = report.get("frame_contract")
    if not isinstance(frame_contract, dict):
        row["failures"].append("missing frame_contract")
        return row
    scene_visual = frame_contract.get("scene_visual_contract")
    if not isinstance(scene_visual, dict):
        row["failures"].append("missing scene_visual_contract")
        return row
    v3 = frame_contract.get("full_scene_shader_pipeline_v3")
    if not isinstance(v3, dict):
        row["failures"].append("missing full_scene_shader_pipeline_v3")
        return row

    manifest_family = manifest_family_by_report.get(str(path), "")
    scene_family = str(scene_visual.get("family", ""))
    family = canonical_family(contract, scene_family, manifest_family)
    family_contracts = contract.get("family_contracts", {})
    family_contract = family_contracts.get(family, {}) if isinstance(family_contracts, dict) else {}
    if not isinstance(family_contract, dict) or not family_contract:
        row["failures"].append(
            f"no scene-local resource contract for scene family {scene_family!r} / manifest family {manifest_family!r}"
        )
        family_contract = {}

    policy_contract = v3.get("scene_profile_policy_contract", {})
    if not isinstance(policy_contract, dict):
        policy_contract = {}
    domains = domain_by_id(v3)
    environment_domain = domains.get("environment", {})
    reflection_domain = domains.get("reflection", {})

    row.update(
        {
            "manifest_family": manifest_family,
            "diagnostic_scope": scope,
            "scene_family": scene_family,
            "contract_family": family,
            "profile_id": scene_visual.get("profile_id", ""),
            "enclosed_scene": bool(scene_visual.get("enclosed_scene", False)),
            "visible_external_hdri_allowed": bool(scene_visual.get("visible_external_hdri_allowed", False)),
            "external_hdri_visible": bool(scene_visual.get("external_hdri_visible", False)),
            "invalid_external_hdri": bool(scene_visual.get("invalid_external_hdri", False)),
            "environment_policy": policy_contract.get("environment_policy", ""),
            "reflection_policy": policy_contract.get("reflection_policy", ""),
            "reflection_source_contract": v3.get("reflection_v3_source_contract", ""),
            "runtime_contract_ready": bool(v3.get("scene_local_resource_contract_ready", False)),
            "runtime_contract_id": v3.get("scene_local_resource_contract_id", ""),
            "runtime_contract_family": v3.get("scene_local_resource_contract_family", ""),
            "runtime_contract_status": v3.get("scene_local_resource_contract_status", ""),
            "runtime_contract_unsafe_reason": v3.get("scene_local_resource_contract_unsafe_reason", ""),
            "runtime_contract_visible_external_hdri_allowed": bool(
                v3.get("scene_local_resource_contract_visible_external_hdri_allowed", False)
            ),
            "runtime_contract_external_hdri_safe": bool(
                v3.get("scene_local_resource_contract_external_hdri_safe", False)
            ),
            "runtime_contract_environment_policy_allowed": bool(
                v3.get("scene_local_resource_contract_environment_policy_allowed", False)
            ),
            "runtime_contract_reflection_policy_allowed": bool(
                v3.get("scene_local_resource_contract_reflection_policy_allowed", False)
            ),
            "runtime_contract_reflection_source_allowed": bool(
                v3.get("scene_local_resource_contract_reflection_source_allowed", False)
            ),
            "runtime_contract_proxy_resources_ready": bool(
                v3.get("scene_local_resource_contract_proxy_resources_ready", False)
            ),
            "runtime_contract_payload_resources_ready": bool(
                v3.get("scene_local_resource_contract_payload_resources_ready", False)
            ),
            "runtime_contract_role_count": int(
                v3.get("scene_local_resource_contract_role_count", 0) or 0
            ),
            "runtime_contract_ready_role_count": int(
                v3.get("scene_local_resource_contract_ready_role_count", 0) or 0
            ),
            "scene_local_environment_ready": bool(v3.get("scene_local_environment_ready", False)),
            "reflection_v3_ready": bool(v3.get("reflection_v3_ready", False)),
            "proxy_bound_resource_count": int(
                v3.get("scene_local_environment_proxy_bound_resource_count", 0) or 0
            ),
            "payload_bound_resource_count": int(
                v3.get("scene_local_texture_payload_bound_resource_count", 0) or 0
            ),
            "proved_roles": [],
        }
    )

    if row["runtime_contract_id"] != "SceneLocalResourceContractV1":
        row["failures"].append("runtime did not report SceneLocalResourceContractV1")
    if row["runtime_contract_family"] != family:
        row["failures"].append(
            f"runtime contract family {row['runtime_contract_family']!r} does not match analyzer family {family!r}"
        )
    if row["runtime_contract_status"] != "ready":
        row["failures"].append(f"runtime contract status is {row['runtime_contract_status']!r}")
    if row["runtime_contract_unsafe_reason"] != "none":
        row["failures"].append(
            f"runtime contract unsafe reason is {row['runtime_contract_unsafe_reason']!r}"
        )
    if row["runtime_contract_ready"] is not True:
        row["failures"].append("runtime scene_local_resource_contract_ready is false")

    required_views: set[str] = set()
    role_contracts = contract.get("role_contracts", {})
    if not isinstance(role_contracts, dict):
        role_contracts = {}
    for role in contract.get("required_roles", []):
        role_id = str(role)
        role_contract = role_contracts.get(role_id, {})
        if not isinstance(role_contract, dict):
            row["failures"].append(f"missing role contract: {role_id}")
            continue
        fields = role_contract.get("required_runtime_fields", [])
        missing_fields = [
            str(field)
            for field in fields
            if not field_is_proved(report, frame_contract, str(field))
        ]
        if missing_fields:
            row["failures"].append(
                f"{role_id} missing runtime proof fields: {', '.join(missing_fields)}"
            )
        else:
            row["proved_roles"].append(role_id)
        for view in role_contract.get("required_debug_views", []):
            if isinstance(view, str) and view:
                required_views.add(view)

    missing_views = sorted(view for view in required_views if view not in requested_views)
    if missing_views:
        row["failures"].append("packet view filter missing resource-contract debug views: " + ", ".join(missing_views))

    if family_contract:
        allowed_environment = set(family_contract.get("allowed_environment_policies", []))
        environment_policy = str(policy_contract.get("environment_policy", ""))
        if allowed_environment and environment_policy not in allowed_environment:
            row["failures"].append(
                f"environment policy {environment_policy!r} is not allowed for {family}"
            )

        allowed_reflection = set(family_contract.get("allowed_reflection_policies", []))
        reflection_policy = str(policy_contract.get("reflection_policy", ""))
        if allowed_reflection and reflection_policy not in allowed_reflection:
            row["failures"].append(
                f"reflection policy {reflection_policy!r} is not allowed for {family}"
            )

        allowed_sources = set(family_contract.get("allowed_reflection_source_contracts", []))
        reflection_source = str(v3.get("reflection_v3_source_contract", ""))
        if allowed_sources and reflection_source not in allowed_sources:
            row["failures"].append(
                f"reflection source contract {reflection_source!r} is not allowed for {family}"
            )

        external_allowed = family_contract.get("visible_external_hdri_allowed") is True
        if not external_allowed:
            if scene_visual.get("visible_external_hdri_allowed") is True:
                row["failures"].append(f"{family} contract forbids visible external HDRI but scene allows it")
            if scene_visual.get("external_hdri_visible") is True:
                row["failures"].append(f"{family} contract forbids visible external HDRI but scene shows it")
        if scene_visual.get("invalid_external_hdri") is True:
            row["failures"].append("scene marks invalid_external_hdri")

        min_proxy = int(family_contract.get("min_proxy_resource_count", 0) or 0)
        if row["proxy_bound_resource_count"] < min_proxy:
            row["failures"].append(
                f"proxy bound resource count {row['proxy_bound_resource_count']} below contract minimum {min_proxy}"
            )
        if row["runtime_contract_proxy_resources_ready"] is not True:
            row["failures"].append("runtime proxy resources are not contract-ready")
        min_payload = int(family_contract.get("min_payload_resource_count", 0) or 0)
        if row["payload_bound_resource_count"] < min_payload:
            row["failures"].append(
                f"payload bound resource count {row['payload_bound_resource_count']} below contract minimum {min_payload}"
            )
        if row["runtime_contract_payload_resources_ready"] is not True:
            row["failures"].append("runtime payload resources are not contract-ready")

    runtime_checks = [
        ("external HDRI", row["runtime_contract_external_hdri_safe"]),
        ("environment policy", row["runtime_contract_environment_policy_allowed"]),
        ("reflection policy", row["runtime_contract_reflection_policy_allowed"]),
        ("reflection source", row["runtime_contract_reflection_source_allowed"]),
    ]
    for label, passed in runtime_checks:
        if passed is not True:
            row["failures"].append(f"runtime {label} contract check failed")
    expected_roles = len(contract.get("required_roles", []))
    if row["runtime_contract_role_count"] != expected_roles:
        row["failures"].append(
            f"runtime role count {row['runtime_contract_role_count']} does not match expected {expected_roles}"
        )
    if row["runtime_contract_ready_role_count"] != expected_roles:
        row["failures"].append(
            f"runtime ready role count {row['runtime_contract_ready_role_count']} does not match expected {expected_roles}"
        )

    if scope != "material_payload":
        if environment_domain.get("ready") is not True:
            row["failures"].append("environment domain is not ready")
        if reflection_domain.get("ready") is not True:
            row["failures"].append("reflection domain is not ready")

    row["ready"] = not row["failures"]
    return row


def build_report(manifest_path: Path, contract_path: Path, min_family_count: int) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    contract = load_json(contract_path)
    failures: list[str] = []
    warnings: list[str] = []

    if contract.get("schema") != "cortex.scene_local_resource_contract.v1":
        failures.append(f"unexpected scene-local resource contract schema: {contract.get('schema')!r}")

    report_family: dict[str, str] = {}
    for result in manifest.get("results", []):
        if not isinstance(result, dict):
            continue
        report = result.get("report")
        family = result.get("family")
        if isinstance(report, str) and isinstance(family, str):
            report_family[report] = family

    requested_views = view_filter_set(manifest)
    rows = [
        analyze_report(path, report_family, contract, requested_views)
        for path in report_paths(manifest)
    ]
    for row in rows:
        failures.extend(f"{row.get('report')}: {failure}" for failure in row.get("failures", []))
        warnings.extend(f"{row.get('report')}: {warning}" for warning in row.get("warnings", []))

    families = {
        str(row.get("contract_family", ""))
        for row in rows
        if known(row.get("contract_family", ""))
    }
    if len(families) < min_family_count:
        failures.append(f"contract family count {len(families)} below required {min_family_count}")

    role_counts: dict[str, int] = {str(role): 0 for role in contract.get("required_roles", [])}
    for row in rows:
        proved = row.get("proved_roles", [])
        if not isinstance(proved, list):
            continue
        for role in proved:
            role_counts[str(role)] = role_counts.get(str(role), 0) + 1

    return {
        "schema": "cortex.scene_local_resource_contract.v1.analysis",
        "manifest": str(manifest_path),
        "contract": str(contract_path),
        "ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "summary": {
            "report_count": len(rows),
            "ready_report_count": sum(1 for row in rows if row.get("ready") is True),
            "contract_family_count": len(families),
            "contract_families": sorted(families),
            "proved_role_counts": role_counts,
        },
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    summary = report.get("summary", {})
    lines = [
        "# Scene-Local Resource Contract V1",
        "",
        f"- manifest: `{report.get('manifest')}`",
        f"- contract: `{report.get('contract')}`",
        f"- ready: `{str(report.get('ready')).lower()}`",
        f"- reports: `{summary.get('report_count', 0)}`",
        f"- ready reports: `{summary.get('ready_report_count', 0)}`",
        f"- contract families: `{', '.join(summary.get('contract_families', []))}`",
        "",
        "| Role | Proved Reports |",
        "|---|---:|",
    ]
    proved_role_counts = summary.get("proved_role_counts", {})
    if isinstance(proved_role_counts, dict):
        for role in sorted(proved_role_counts):
            lines.append(f"| {role} | {proved_role_counts[role]} |")
    lines.extend(
        [
            "",
        "| Report | Family | Ready | Runtime | External HDRI | Reflection Source | Proxy/Texture Resources |",
        "|---|---|---|---|---|---|---|",
        ]
    )
    for row in report.get("rows", []):
        if not isinstance(row, dict):
            continue
        external = (
            f"allowed={str(row.get('visible_external_hdri_allowed')).lower()}, "
            f"visible={str(row.get('external_hdri_visible')).lower()}, "
            f"invalid={str(row.get('invalid_external_hdri')).lower()}"
        )
        resources = f"{row.get('proxy_bound_resource_count', 0)}/{row.get('payload_bound_resource_count', 0)}"
        runtime = (
            f"{row.get('runtime_contract_status', '')}:"
            f"{row.get('runtime_contract_unsafe_reason', '')}"
        )
        lines.append(
            "| "
            + " | ".join(
                [
                    f"`{Path(str(row.get('report', ''))).name}`",
                    f"`{row.get('contract_family', '')}`",
                    f"`{str(row.get('ready')).lower()}`",
                    f"`{runtime}`",
                    f"`{external}`",
                    f"`{row.get('reflection_source_contract', '')}`",
                    f"`{resources}`",
                ]
            )
            + " |"
        )
    if report.get("failures"):
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in report["failures"])
    if report.get("warnings"):
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in report["warnings"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--contract", default=DEFAULT_CONTRACT, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    parser.add_argument("--min-family-count", type=int, default=1)
    args = parser.parse_args()

    report = build_report(args.manifest, args.contract, args.min_family_count)
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_markdown(report, args.output_md)
    if report["failures"]:
        print(f"FAIL: Scene-local resource contract has failures={len(report['failures'])}")
        print(f"json={args.output_json}")
        print(f"markdown={args.output_md}")
        return 1
    print("PASS: Scene-local resource contract is satisfied")
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
