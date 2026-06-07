#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


UNKNOWN_VALUES = {"", "unknown", "none", "default"}


def load_json(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def report_paths(manifest: dict[str, Any]) -> list[Path]:
    paths: list[Path] = []
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        report = row.get("report")
        if isinstance(report, str) and report:
            paths.append(Path(report))
    return paths


def parse_required_profile(spec: str) -> tuple[str, float | None]:
    parts = spec.split("=", 1)
    profile = parts[0].strip()
    if not profile:
        raise ValueError(f"invalid empty required profile spec: {spec!r}")
    if len(parts) == 1 or not parts[1].strip():
        return profile, None
    return profile, float(parts[1].strip())


def analyze_report(path: Path, manifest_path: Path) -> dict[str, Any]:
    report = load_json(path)
    contract = report.get("frame_contract", {})
    scene_visual = contract.get("scene_visual_contract", {})
    v3 = contract.get("full_scene_shader_pipeline_v3", {})
    policy_contract = v3.get("scene_profile_policy_contract", {})
    if not isinstance(policy_contract, dict):
        policy_contract = {}
    shader_profile = str(v3.get("scene_local_environment_shader_profile", "unknown"))
    shader_profile_mode = float(v3.get("scene_local_environment_shader_profile_mode", -1.0) or -1.0)
    local_background_strength = float(
        v3.get("scene_local_environment_local_background_strength", -1.0) or -1.0
    )
    row = {
        "manifest": str(manifest_path),
        "report": str(path),
        "family": scene_visual.get("family", "unknown"),
        "profile_id": scene_visual.get("profile_id", "unknown"),
        "policy_contract_id": policy_contract.get("contract_id", "unknown"),
        "policy_enclosure_mode": policy_contract.get("enclosure_mode", "unknown"),
        "policy_environment": policy_contract.get("environment_policy", "unknown"),
        "policy_reflection": policy_contract.get("reflection_policy", "unknown"),
        "environment_ready": v3.get("scene_local_environment_ready") is True,
        "shader_profile": shader_profile,
        "shader_profile_mode": shader_profile_mode,
        "local_background_strength": local_background_strength,
        "failures": [],
    }
    if row["environment_ready"]:
        if shader_profile.strip().lower() in UNKNOWN_VALUES:
            row["failures"].append("environment ready without shader profile")
        if shader_profile_mode < 0.0 or shader_profile_mode > 4.0:
            row["failures"].append("shader profile mode out of range")
        if local_background_strength < 0.0 or local_background_strength > 1.0:
            row["failures"].append("local background strength out of range")
    return row


def write_markdown(path: Path, result: dict[str, Any]) -> None:
    lines = [
        "# V3 Scene-Local Environment Shader Profiles",
        "",
        f"- manifests: `{len(result['manifests'])}`",
        f"- reports: `{result['report_count']}`",
        f"- environment-ready reports: `{result['environment_ready_report_count']}`",
        f"- distinct shader profiles: `{', '.join(result['distinct_shader_profiles'])}`",
        f"- distinct shader modes: `{', '.join(str(mode) for mode in result['distinct_shader_modes'])}`",
        f"- failures: `{len(result['failures'])}`",
        "",
        "| Family | Profile Id | Shader Profile | Mode | Local Background | Enclosure | Environment Policy |",
        "|---|---|---|---:|---:|---|---|",
    ]
    for row in result["rows"]:
        lines.append(
            "| "
            + " | ".join(
                [
                    str(row["family"]),
                    f"`{row['profile_id']}`",
                    f"`{row['shader_profile']}`",
                    f"{row['shader_profile_mode']:.1f}",
                    f"{row['local_background_strength']:.2f}",
                    f"`{row['policy_enclosure_mode']}`",
                    f"`{row['policy_environment']}`",
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
    parser.add_argument("--manifest", action="append", required=True, type=Path)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    parser.add_argument("--min-ready-reports", type=int, default=1)
    parser.add_argument("--min-distinct-modes", type=int, default=1)
    parser.add_argument("--min-distinct-profiles", type=int, default=1)
    parser.add_argument(
        "--allow-missing-reports",
        action="store_true",
        help="Allow diagnostic manifests from failed renderer runs to skip missing report paths.",
    )
    parser.add_argument(
        "--require-profile",
        action="append",
        default=[],
        help="Required shader profile, optionally as profile=mode.",
    )
    args = parser.parse_args()

    rows: list[dict[str, Any]] = []
    failures: list[str] = []
    for manifest_path in args.manifest:
        manifest = load_json(manifest_path)
        for report_path in report_paths(manifest):
            if not report_path.exists():
                if not args.allow_missing_reports:
                    failures.append(f"missing frame report: {report_path}")
                continue
            row = analyze_report(report_path, manifest_path)
            rows.append(row)
            failures.extend(f"{row['report']}: {failure}" for failure in row["failures"])

    ready_rows = [row for row in rows if row["environment_ready"]]
    distinct_profiles = sorted(
        {
            str(row["shader_profile"])
            for row in ready_rows
            if str(row["shader_profile"]).strip().lower() not in UNKNOWN_VALUES
        }
    )
    distinct_modes = sorted({float(row["shader_profile_mode"]) for row in ready_rows})

    if len(ready_rows) < args.min_ready_reports:
        failures.append(
            f"environment-ready report count {len(ready_rows)} is below required {args.min_ready_reports}"
        )
    if len(distinct_modes) < args.min_distinct_modes:
        failures.append(
            f"distinct shader mode count {len(distinct_modes)} is below required {args.min_distinct_modes}"
        )
    if len(distinct_profiles) < args.min_distinct_profiles:
        failures.append(
            f"distinct shader profile count {len(distinct_profiles)} is below required {args.min_distinct_profiles}"
        )

    profile_to_modes: dict[str, set[float]] = {}
    for row in ready_rows:
        profile = str(row["shader_profile"])
        profile_to_modes.setdefault(profile, set()).add(float(row["shader_profile_mode"]))
    for required in args.require_profile:
        profile, expected_mode = parse_required_profile(required)
        if profile not in profile_to_modes:
            failures.append(f"required shader profile missing: {profile}")
            continue
        if expected_mode is not None and expected_mode not in profile_to_modes[profile]:
            modes = ", ".join(str(mode) for mode in sorted(profile_to_modes[profile]))
            failures.append(
                f"required shader profile {profile} did not use mode {expected_mode}; observed: {modes}"
            )

    result = {
        "schema": "cortex.full_scene_shader_pipeline_v3.environment_profiles.v1",
        "manifests": [str(path) for path in args.manifest],
        "report_count": len(rows),
        "environment_ready_report_count": len(ready_rows),
        "distinct_shader_profiles": distinct_profiles,
        "distinct_shader_modes": distinct_modes,
        "rows": rows,
        "failures": failures,
    }
    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    write_markdown(args.output_md, result)
    if failures:
        print(f"FAIL: V3 environment shader profile analysis found {len(failures)} failure(s)")
        print(f"json={args.output_json}")
        print(f"markdown={args.output_md}")
        return 1
    print("PASS: V3 environment shader profiles are measurable")
    print(f"json={args.output_json}")
    print(f"markdown={args.output_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
