#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
from typing import Any


DOMAINS = [
    "material",
    "lighting",
    "environment",
    "reflection",
    "composite",
    "cinematic_post",
]


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def find_frame_reports(root: pathlib.Path) -> list[pathlib.Path]:
    reports: list[pathlib.Path] = []
    seen: set[pathlib.Path] = set()
    for name in ("frame_report_shutdown.json", "frame_report_last.json"):
        for path in root.rglob(name):
            resolved = path.resolve()
            if resolved not in seen:
                seen.add(resolved)
                reports.append(path)
    return sorted(reports)


def manifest_failures(root: pathlib.Path) -> dict[str, dict[str, Any]]:
    manifest = root / "manifest.json"
    if not manifest.exists():
        return {}
    data = load_json(manifest)
    failures: dict[str, dict[str, Any]] = {}
    for row in data.get("results", []):
        if not isinstance(row, dict):
            continue
        report = row.get("report")
        if not isinstance(report, str) or not report:
            continue
        if row.get("passed") is False or row.get("exit_code") not in (0, None):
            failures[str(pathlib.Path(report).resolve())] = row
    return failures


def domain_ids(v3: dict[str, Any]) -> set[str]:
    ready: set[str] = set()
    for domain in v3.get("domains", []):
        if not isinstance(domain, dict):
            continue
        if domain.get("ready") is True and isinstance(domain.get("id"), str):
            ready.add(domain["id"])
    return ready


def temporal_channel(v3: dict[str, Any]) -> str:
    for domain in v3.get("domains", []):
        if not isinstance(domain, dict) or domain.get("id") != "reflection":
            continue
        for channel in domain.get("channels", []):
            if isinstance(channel, str) and channel.startswith("reflection_temporal_delta_"):
                return channel
    return "missing"


def summarize(root: pathlib.Path) -> dict[str, Any]:
    failures_by_report = manifest_failures(root)
    rows: list[dict[str, Any]] = []
    counts = {domain: 0 for domain in DOMAINS}
    temporal_counts: dict[str, int] = {}
    for report in find_frame_reports(root):
        data = load_json(report)
        contract = data.get("frame_contract", data)
        v3 = contract.get("full_scene_shader_pipeline_v3", {})
        ready = domain_ids(v3)
        for domain in DOMAINS:
            counts[domain] += 1 if domain in ready else 0
        temporal = temporal_channel(v3)
        temporal_counts[temporal] = temporal_counts.get(temporal, 0) + 1
        report_key = str(report.resolve())
        rows.append(
            {
                "report": str(report),
                "relative_report": str(report.relative_to(root)),
                "packet_failed": report_key in failures_by_report,
                "exit_code": failures_by_report.get(report_key, {}).get("exit_code"),
                "family": failures_by_report.get(report_key, {}).get("family"),
                "view": failures_by_report.get(report_key, {}).get("view"),
                "ready_domains": sorted(ready),
                "missing_domains": sorted(set(DOMAINS) - ready),
                "reflection_source": v3.get("reflection_v3_source_contract"),
                "reflection_temporal_channel": temporal,
                "reflection_v3_ready": v3.get("reflection_v3_ready"),
                "composite_v3_ready": v3.get("composite_v3_ready"),
                "cinematic_post_v3_ready": v3.get("cinematic_post_v3_ready"),
            }
        )
    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.frame_report_summary.v1",
        "packet_root": str(root),
        "report_count": len(rows),
        "ready_domain_report_counts": counts,
        "reflection_temporal_channel_counts": temporal_counts,
        "packet_failed_report_count": sum(1 for row in rows if row["packet_failed"]),
        "rows": rows,
    }


def write_markdown(path: pathlib.Path, summary: dict[str, Any]) -> None:
    lines = [
        "# Full Scene Shader Pipeline V3 Frame Report Summary",
        "",
        f"- packet root: `{summary['packet_root']}`",
        f"- report count: `{summary['report_count']}`",
        f"- packet failed report count: `{summary['packet_failed_report_count']}`",
        "",
        "| Domain | Ready Reports |",
        "|---|---:|",
    ]
    for domain, count in summary["ready_domain_report_counts"].items():
        lines.append(f"| {domain} | {count} |")
    lines.extend(["", "| Reflection Temporal Channel | Reports |", "|---|---:|"])
    for channel, count in sorted(summary["reflection_temporal_channel_counts"].items()):
        lines.append(f"| {channel} | {count} |")
    lines.extend(
        [
            "",
            "| Report | Failed | Missing Domains | Reflection Source | Temporal Channel |",
            "|---|---:|---|---|---|",
        ]
    )
    for row in summary["rows"]:
        missing = ",".join(row["missing_domains"])
        lines.append(
            "| "
            + " | ".join(
                [
                    f"`{row['relative_report']}`",
                    str(row["packet_failed"]).lower(),
                    missing,
                    str(row["reflection_source"]),
                    str(row["reflection_temporal_channel"]),
                ]
            )
            + " |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-md", required=True)
    args = parser.parse_args()

    root = pathlib.Path(args.input)
    summary = summarize(root)

    output_json = pathlib.Path(args.output_json)
    output_md = pathlib.Path(args.output_md)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    write_markdown(output_md, summary)

    print(f"frame_report_summary={output_json}")
    print(f"frame_report_summary_md={output_md}")
    print(
        "PASS: summarized "
        f"{summary['report_count']} V3 frame reports "
        f"({summary['packet_failed_report_count']} packet-failed reports)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
