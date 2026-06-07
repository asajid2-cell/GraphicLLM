#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any


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


def analyze_report(path: Path) -> dict[str, Any]:
    report = load_json(path)
    contract = report.get("frame_contract", {})
    environment = contract.get("environment", {})
    v3 = contract.get("full_scene_shader_pipeline_v3", {})
    scene_visual = contract.get("scene_visual_contract", {})
    row = {
        "report": str(path),
        "family": scene_visual.get("family", "unknown"),
        "profile_id": scene_visual.get("profile_id", "unknown"),
        "environment_ready": v3.get("scene_local_environment_ready") is True,
        "texture_set_id": environment.get("scene_local_texture_set_id", "none"),
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
        "failures": [],
    }
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
    return row


def write_markdown(path: Path, result: dict[str, Any]) -> None:
    lines = [
        "# V3 Scene-Local Environment Payload",
        "",
        f"- manifest: `{result['manifest']}`",
        f"- report count: `{result['report_count']}`",
        f"- texture-set-present reports: `{result['texture_set_present_report_count']}`",
        f"- payload-ready reports: `{result['payload_ready_report_count']}`",
        f"- failures: `{len(result['failures'])}`",
        "",
        "| Family | Texture Set | Textures | Albedo | Normal | Payload | Proxies |",
        "|---|---|---:|---:|---:|---|---|",
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
                    f"`{row['texture_set_id']}`",
                    str(row["texture_count"]),
                    str(row["albedo_count"]),
                    str(row["normal_count"]),
                    str(row["payload_ready"]).lower(),
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
    rows = [analyze_report(path) for path in report_paths(manifest)]
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
