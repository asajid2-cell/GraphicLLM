#!/usr/bin/env python3
"""Gate SceneProfileV3 policy ownership from scene-local packet reports."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


REQUIRED_SCENE_VISUAL_FIELDS = [
    "profile_id",
    "family",
    "environment_owner",
    "reflection_owner",
    "light_rig_id",
    "shadow_policy_id",
    "exposure_policy_id",
    "material_palette_id",
    "lighting_script_id",
    "material_class_set_id",
    "material_layer_set_id",
    "temporal_policy_id",
    "post_policy_id",
    "post_quality_set_id",
    "tone_mapper_preset",
]


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def known(value: Any) -> bool:
    if not isinstance(value, str):
        return False
    normalized = value.strip().lower()
    return normalized not in {"", "unknown", "none", "default", "unprofiled"}


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


def domain_by_id(v3: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(domain.get("id", "")): domain
        for domain in v3.get("domains", [])
        if isinstance(domain, dict)
    }


def analyze_report(path: Path) -> tuple[dict[str, Any], list[str], list[str]]:
    failures: list[str] = []
    warnings: list[str] = []
    if not path.exists():
        return {"report": str(path), "status": "missing"}, [f"{path}: missing report"], warnings

    report = load_json(path)
    frame_contract = report.get("frame_contract")
    if not isinstance(frame_contract, dict):
        return {"report": str(path), "status": "missing_frame_contract"}, [f"{path}: missing frame_contract"], warnings

    scene_visual = frame_contract.get("scene_visual_contract")
    if not isinstance(scene_visual, dict):
        return {"report": str(path), "status": "missing_scene_visual_contract"}, [f"{path}: missing scene_visual_contract"], warnings

    v3 = frame_contract.get("full_scene_shader_pipeline_v3")
    if not isinstance(v3, dict):
        return {"report": str(path), "status": "missing_v3"}, [f"{path}: missing full_scene_shader_pipeline_v3"], warnings

    domains = domain_by_id(v3)
    scene_profile = domains.get("scene_profile")
    if not isinstance(scene_profile, dict):
        failures.append(f"{path}: V3 scene_profile domain missing")
        scene_profile = {}

    status = "ok"
    if scene_visual.get("active") is not True:
        failures.append(f"{path}: scene_visual_contract is not active")
        status = "failed"
    if scene_visual.get("invalid_external_hdri") is True:
        failures.append(f"{path}: scene_visual_contract marks invalid_external_hdri")
        status = "failed"
    for field in REQUIRED_SCENE_VISUAL_FIELDS:
        if not known(scene_visual.get(field)):
            failures.append(f"{path}: scene_visual_contract.{field} is not owned/known")
            status = "failed"

    if v3.get("scene_profile_ready") is not True:
        failures.append(f"{path}: full_scene_shader_pipeline_v3.scene_profile_ready is not true")
        status = "failed"
    if int(v3.get("scene_profile_policy_count", 0) or 0) <= 0:
        failures.append(f"{path}: full_scene_shader_pipeline_v3.scene_profile_policy_count is zero")
        status = "failed"

    if scene_profile.get("enabled") is not True:
        failures.append(f"{path}: V3 scene_profile domain is not enabled")
        status = "failed"
    if scene_profile.get("ready") is not True:
        failures.append(f"{path}: V3 scene_profile domain is not ready")
        status = "failed"
    if scene_profile.get("producer") != "SceneCinematicProfileV1Adapter":
        failures.append(f"{path}: V3 scene_profile producer is {scene_profile.get('producer')!r}")
        status = "failed"
    if scene_profile.get("output_resource") != "scene_visual_contract":
        failures.append(f"{path}: V3 scene_profile output is {scene_profile.get('output_resource')!r}")
        status = "failed"
    if int(scene_profile.get("missing_required_channel_count", 0) or 0) != 0:
        failures.append(f"{path}: V3 scene_profile missing_required_channel_count is nonzero")
        status = "failed"

    row = {
        "report": str(path),
        "status": status,
        "profile_id": scene_visual.get("profile_id", ""),
        "family": scene_visual.get("family", ""),
        "enclosed_scene": bool(scene_visual.get("enclosed_scene", False)),
        "environment_owner": scene_visual.get("environment_owner", ""),
        "reflection_owner": scene_visual.get("reflection_owner", ""),
        "light_rig_id": scene_visual.get("light_rig_id", ""),
        "shadow_policy_id": scene_visual.get("shadow_policy_id", ""),
        "exposure_policy_id": scene_visual.get("exposure_policy_id", ""),
        "material_palette_id": scene_visual.get("material_palette_id", ""),
        "lighting_script_id": scene_visual.get("lighting_script_id", ""),
        "material_class_set_id": scene_visual.get("material_class_set_id", ""),
        "material_layer_set_id": scene_visual.get("material_layer_set_id", ""),
        "temporal_policy_id": scene_visual.get("temporal_policy_id", ""),
        "post_policy_id": scene_visual.get("post_policy_id", ""),
        "post_quality_set_id": scene_visual.get("post_quality_set_id", ""),
        "tone_mapper_preset": scene_visual.get("tone_mapper_preset", ""),
        "profile_light_fixture_count": int(scene_visual.get("profile_light_fixture_count", 0) or 0),
        "v3_scene_profile_ready": bool(v3.get("scene_profile_ready", False)),
        "v3_scene_profile_policy_count": int(v3.get("scene_profile_policy_count", 0) or 0),
    }
    return row, failures, warnings


def build_report(manifest_path: Path, min_family_count: int) -> dict[str, Any]:
    manifest = load_json(manifest_path)
    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    warnings: list[str] = []

    for path in report_paths(manifest):
        row, row_failures, row_warnings = analyze_report(path)
        rows.append(row)
        failures.extend(row_failures)
        warnings.extend(row_warnings)

    families = {str(row.get("family", "")) for row in rows if known(row.get("family"))}
    profile_ids = {str(row.get("profile_id", "")) for row in rows if known(row.get("profile_id"))}
    environment_owners = {str(row.get("environment_owner", "")) for row in rows if known(row.get("environment_owner"))}
    reflection_owners = {str(row.get("reflection_owner", "")) for row in rows if known(row.get("reflection_owner"))}
    light_rigs = {str(row.get("light_rig_id", "")) for row in rows if known(row.get("light_rig_id"))}
    material_palettes = {str(row.get("material_palette_id", "")) for row in rows if known(row.get("material_palette_id"))}

    if len(families) < min_family_count:
        failures.append(f"captured family count {len(families)} below required {min_family_count}")
    if len(profile_ids) < min_family_count:
        failures.append(f"distinct profile_id count {len(profile_ids)} below required {min_family_count}")
    if len(light_rigs) < 2 and min_family_count > 1:
        failures.append("scene profile light_rig_id does not vary across captured families")
    if len(material_palettes) < 2 and min_family_count > 1:
        failures.append("scene profile material_palette_id does not vary across captured families")

    summary = {
        "report_count": len(rows),
        "family_count": len(families),
        "profile_id_count": len(profile_ids),
        "environment_owner_count": len(environment_owners),
        "reflection_owner_count": len(reflection_owners),
        "light_rig_count": len(light_rigs),
        "material_palette_count": len(material_palettes),
    }
    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.scene_profile.v1",
        "manifest": str(manifest_path),
        "ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "summary": summary,
        "rows": rows,
    }


def write_markdown(report: dict[str, Any], path: Path) -> None:
    summary = report.get("summary", {})
    lines = [
        "# V3 Scene Profile Diagnostics",
        "",
        f"- manifest: `{report.get('manifest')}`",
        f"- ready: `{str(report.get('ready')).lower()}`",
        f"- failures: {len(report.get('failures', []))}",
        f"- warnings: {len(report.get('warnings', []))}",
        f"- reports: {summary.get('report_count', 0)}",
        f"- families: {summary.get('family_count', 0)}",
        f"- profiles: {summary.get('profile_id_count', 0)}",
        f"- light rigs: {summary.get('light_rig_count', 0)}",
        f"- material palettes: {summary.get('material_palette_count', 0)}",
        "",
        "| Family | Profile | Environment | Reflection | Light Rig | Material Palette | Fixture Count | Status |",
        "|---|---|---|---|---|---|---:|---|",
    ]
    for row in report.get("rows", []):
        lines.append(
            "| {family} | {profile} | {environment} | {reflection} | {light_rig} | {palette} | {fixtures} | {status} |".format(
                family=row.get("family", ""),
                profile=row.get("profile_id", ""),
                environment=row.get("environment_owner", ""),
                reflection=row.get("reflection_owner", ""),
                light_rig=row.get("light_rig_id", ""),
                palette=row.get("material_palette_id", ""),
                fixtures=int(row.get("profile_light_fixture_count", 0) or 0),
                status=row.get("status", ""),
            )
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
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    parser.add_argument("--min-family-count", type=int, default=3)
    args = parser.parse_args()

    report = build_report(args.manifest, args.min_family_count)
    args.output_json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    write_markdown(report, args.output_md)
    if report["failures"]:
        for failure in report["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: V3 scene profile policy ownership is measurable")
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
