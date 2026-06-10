#!/usr/bin/env python3
"""Build a promotion-grade scene-local resource contract matrix.

Tool marker: build_scene_local_resource_contract_matrix.py.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REQUIRED_ROLES = [
    "diffuse_irradiance",
    "specular_radiance",
    "visible_background",
    "reflection_background",
    "atmosphere",
    "exposure",
]


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def normalize_packet_root(raw: str) -> Path:
    path = Path(raw)
    if path.is_absolute():
        return path
    for candidate in (Path.cwd() / path, ROOT / path):
        if candidate.exists():
            return candidate.resolve()
    return (ROOT / path).resolve()


def load_manifest_motion(packet_root: Path) -> str:
    manifest_path = packet_root / "manifest.json"
    if not manifest_path.exists():
        return "unknown"
    manifest = load_json(manifest_path)
    return str(manifest.get("stability_motion_mode", "static"))


def load_manifest_families(packet_root: Path) -> list[str]:
    manifest_path = packet_root / "manifest.json"
    if not manifest_path.exists():
        return []
    manifest = load_json(manifest_path)
    families = {
        str(row.get("family", ""))
        for row in manifest.get("results", [])
        if isinstance(row, dict) and row.get("family")
    }
    return sorted(families)


def packet_row(packet_root: Path, required_roles: list[str]) -> dict[str, Any]:
    contract_path = packet_root / "scene_local_resource_contract_v1.json"
    row: dict[str, Any] = {
        "packet_root": str(packet_root),
        "scene_local_resource_contract": str(contract_path),
        "exists": packet_root.exists(),
        "motion_mode": load_manifest_motion(packet_root),
        "manifest_families": load_manifest_families(packet_root),
        "ready": False,
        "report_count": 0,
        "ready_report_count": 0,
        "contract_families": [],
        "proved_role_counts": {},
        "external_hdri_violation_count": 0,
        "unsafe_reason_counts": {},
        "reflection_source_contracts": [],
        "failures": [],
        "warnings": [],
    }
    if not packet_root.exists():
        row["failures"].append("packet root missing")
        return row
    if not contract_path.exists():
        row["failures"].append("scene_local_resource_contract_v1.json missing")
        return row

    report = load_json(contract_path)
    summary = report.get("summary", {})
    if not isinstance(summary, dict):
        summary = {}
    rows = report.get("rows", [])
    if not isinstance(rows, list):
        rows = []

    row["ready"] = report.get("ready") is True
    row["report_count"] = int(summary.get("report_count", 0) or 0)
    row["ready_report_count"] = int(summary.get("ready_report_count", 0) or 0)
    row["contract_families"] = list(summary.get("contract_families", []))
    role_counts = summary.get("proved_role_counts", {})
    row["proved_role_counts"] = role_counts if isinstance(role_counts, dict) else {}

    unsafe_reason_counts: dict[str, int] = {}
    reflection_sources: set[str] = set()
    external_violations = 0
    for item in rows:
        if not isinstance(item, dict):
            continue
        unsafe = str(item.get("runtime_contract_unsafe_reason", ""))
        if unsafe and unsafe != "none":
            unsafe_reason_counts[unsafe] = unsafe_reason_counts.get(unsafe, 0) + 1
        if item.get("external_hdri_visible") is True or item.get("invalid_external_hdri") is True:
            external_violations += 1
        source = str(item.get("reflection_source_contract", ""))
        if source:
            reflection_sources.add(source)
    row["external_hdri_violation_count"] = external_violations
    row["unsafe_reason_counts"] = dict(sorted(unsafe_reason_counts.items()))
    row["reflection_source_contracts"] = sorted(reflection_sources)

    for failure in report.get("failures", []):
        row["failures"].append(f"resource_contract: {failure}")
    for warning in report.get("warnings", []):
        row["warnings"].append(f"resource_contract: {warning}")
    if row["ready"] is not True:
        row["failures"].append("resource contract report is not ready")
    if row["report_count"] <= 0:
        row["failures"].append("resource contract report has no frame reports")
    if row["ready_report_count"] != row["report_count"]:
        row["failures"].append(
            f"ready report count {row['ready_report_count']} does not match report count {row['report_count']}"
        )
    if external_violations:
        row["failures"].append(f"external HDRI violations present: {external_violations}")
    if unsafe_reason_counts:
        row["failures"].append("unsafe runtime reasons present")
    for role in required_roles:
        count = int(row["proved_role_counts"].get(role, 0) or 0)
        if count != row["report_count"]:
            row["failures"].append(
                f"role {role} proved {count} reports, expected {row['report_count']}"
            )
    return row


def build_matrix(
    packet_roots: list[Path],
    *,
    required_families: list[str],
    required_motion_modes: list[str],
    required_roles: list[str],
) -> dict[str, Any]:
    rows = [packet_row(root, required_roles) for root in packet_roots]
    ready_rows = [row for row in rows if row.get("ready") is True and not row.get("failures")]
    observed_families = sorted(
        {
            family
            for row in ready_rows
            for family in (list(row.get("contract_families", [])) + list(row.get("manifest_families", [])))
            if isinstance(family, str) and family
        }
    )
    observed_motion_modes = sorted(
        {
            str(row.get("motion_mode"))
            for row in ready_rows
            if row.get("motion_mode") not in (None, "", "unknown")
        }
    )

    failures: list[str] = []
    warnings: list[str] = []
    role_totals: dict[str, int] = {role: 0 for role in required_roles}
    reflection_sources: set[str] = set()
    total_reports = 0
    total_ready_reports = 0
    for row in rows:
        total_reports += int(row.get("report_count", 0) or 0)
        total_ready_reports += int(row.get("ready_report_count", 0) or 0)
        for role, count in row.get("proved_role_counts", {}).items():
            role_totals[str(role)] = role_totals.get(str(role), 0) + int(count or 0)
        for source in row.get("reflection_source_contracts", []):
            reflection_sources.add(str(source))
        for failure in row.get("failures", []):
            failures.append(f"{row.get('packet_root')}: {failure}")
        for warning in row.get("warnings", []):
            warnings.append(f"{row.get('packet_root')}: {warning}")

    missing_families = sorted(set(required_families) - set(observed_families))
    missing_motion_modes = sorted(set(required_motion_modes) - set(observed_motion_modes))
    if missing_families:
        failures.append("missing required scene-local families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing required scene-local motion modes: " + ", ".join(missing_motion_modes))
    if total_ready_reports != total_reports:
        failures.append(f"total ready reports {total_ready_reports} does not match total reports {total_reports}")
    for role in required_roles:
        if role_totals.get(role, 0) != total_reports:
            failures.append(
                f"aggregate role {role} proved {role_totals.get(role, 0)} reports, expected {total_reports}"
            )

    return {
        "schema": "cortex.scene_local_resource_contract.promotion_matrix.v1",
        "packet_count": len(rows),
        "ready_packet_count": len(ready_rows),
        "required_families": required_families,
        "required_motion_modes": required_motion_modes,
        "required_roles": required_roles,
        "observed_families": observed_families,
        "missing_families": missing_families,
        "observed_motion_modes": observed_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "scene_local_resource_contract_ready": not failures,
        "total_report_count": total_reports,
        "total_ready_report_count": total_ready_reports,
        "aggregate_proved_role_counts": dict(sorted(role_totals.items())),
        "observed_reflection_source_contracts": sorted(reflection_sources),
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(matrix: dict[str, Any], output: Path) -> None:
    lines = [
        "# Scene-Local Resource Contract Promotion Matrix",
        "",
        f"- packet count: `{matrix['packet_count']}`",
        f"- ready packets: `{matrix['ready_packet_count']}`",
        f"- scene-local resource contract ready: `{str(matrix['scene_local_resource_contract_ready']).lower()}`",
        f"- total reports: `{matrix['total_report_count']}`",
        f"- ready reports: `{matrix['total_ready_report_count']}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- observed reflection source contracts: `{', '.join(matrix['observed_reflection_source_contracts'])}`",
        f"- failures: `{len(matrix['failures'])}`",
        f"- warnings: `{len(matrix['warnings'])}`",
        "",
        "## Proved Roles",
        "",
        "| Role | Proved Reports |",
        "|---|---:|",
    ]
    for role, count in matrix["aggregate_proved_role_counts"].items():
        lines.append(f"| {role} | {count} |")
    lines.extend(
        [
            "",
            "| Packet | Motion | Families | Ready | Reports | Ready Reports | Reflection Sources | External HDRI Violations | Failures | Warnings |",
            "|---|---|---|---:|---:|---:|---|---:|---:|---:|",
        ]
    )
    for row in matrix["rows"]:
        lines.append(
            "| {packet} | {motion} | {families} | {ready} | {reports} | {ready_reports} | {sources} | {external} | {failures} | {warnings} |".format(
                packet=row["packet_root"],
                motion=row["motion_mode"],
                families=", ".join(row.get("manifest_families", []) or row.get("contract_families", [])),
                ready=str(bool(row.get("ready"))).lower(),
                reports=row.get("report_count", 0),
                ready_reports=row.get("ready_report_count", 0),
                sources=", ".join(row.get("reflection_source_contracts", [])),
                external=row.get("external_hdri_violation_count", 0),
                failures=len(row.get("failures", [])),
                warnings=len(row.get("warnings", [])),
            )
        )
    if matrix["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in matrix["failures"])
    if matrix["warnings"]:
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in matrix["warnings"])
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packet-root", action="append", required=True)
    parser.add_argument(
        "--required-families",
        default="stress_rt_showcase_reflection_closeup,gallery,kitchen,office,gym,concert,red_room,stadium",
    )
    parser.add_argument("--required-motion-modes", default="static,mouse_jitter,camera_sweep,light_sweep")
    parser.add_argument("--required-roles", default=",".join(DEFAULT_REQUIRED_ROLES))
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    matrix = build_matrix(
        [normalize_packet_root(root) for root in args.packet_root],
        required_families=split_csv(args.required_families),
        required_motion_modes=split_csv(args.required_motion_modes),
        required_roles=split_csv(args.required_roles),
    )

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")
    write_markdown(matrix, args.output_md)

    print(f"scene_local_matrix={args.output_json}")
    print(f"scene_local_matrix_md={args.output_md}")
    if not matrix["scene_local_resource_contract_ready"]:
        for failure in matrix["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: Scene-local resource contract matrix is fully covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
