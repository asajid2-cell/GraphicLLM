#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


REQUIRED_DOMAINS = {
    "material",
    "lighting",
    "environment",
    "reflection",
    "composite",
    "cinematic_post",
}

DEFAULT_REQUIRED_FAMILIES = [
    "gallery",
    "kitchen",
    "office",
    "gym",
    "concert",
    "red_room",
    "stadium",
]

DEFAULT_REQUIRED_MOTION_MODES = ["static", "mouse_jitter", "camera_sweep"]


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8-sig"))


def split_csv(value: str) -> list[str]:
    return [item.strip() for item in value.split(",") if item.strip()]


def captured_families(manifest: dict[str, Any]) -> list[str]:
    families: set[str] = set()
    for row in manifest.get("results", []):
        if not isinstance(row, dict):
            continue
        family = row.get("family")
        if isinstance(family, str) and family:
            families.add(family)
    return sorted(families)


def find_ready_domains(signal: dict[str, Any]) -> dict[str, set[str]]:
    ready: dict[str, set[str]] = {}
    for row in signal.get("rows", []):
        if not isinstance(row, dict):
            continue
        report = str(row.get("report", ""))
        domains = row.get("ready_domains", [])
        if not isinstance(domains, list):
            domains = []
        ready[report] = {str(domain) for domain in domains}
    return ready


def make_decision(
    *,
    packet_root: pathlib.Path,
    required_families: list[str],
    required_motion_modes: list[str],
    allow_subset_review: bool,
) -> dict[str, Any]:
    manifest_path = packet_root / "manifest.json"
    signal_path = packet_root / "v3_signal.json"
    stability_path = packet_root / "v3_stability.json"
    lighting_motion_path = packet_root / "v3_lighting_motion.json"

    failures: list[str] = []
    warnings: list[str] = []
    evidence: dict[str, Any] = {
        "manifest": str(manifest_path),
        "signal": str(signal_path),
        "stability": str(stability_path),
        "lighting_motion": str(lighting_motion_path) if lighting_motion_path.exists() else None,
    }

    if not manifest_path.exists():
        failures.append(f"missing manifest: {manifest_path}")
    if not signal_path.exists():
        failures.append(f"missing v3_signal.json: {signal_path}")
    if not stability_path.exists():
        failures.append(f"missing v3_stability.json: {stability_path}")
    if failures:
        return {
            "schema": "cortex.full_scene_shader_pipeline_v3.promotion_decision.v1",
            "packet_root": str(packet_root),
            "status": "blocked",
            "default_beauty_promotable": False,
            "review_packet_passed": False,
            "failures": failures,
            "warnings": warnings,
            "evidence": evidence,
        }

    manifest = load_json(manifest_path)
    signal = load_json(signal_path)
    stability = load_json(stability_path)
    lighting_motion = load_json(lighting_motion_path) if lighting_motion_path.exists() else None

    signal_failures = [str(item) for item in signal.get("failures", [])]
    stability_failures = [str(item) for item in stability.get("failures", [])]
    signal_warnings = [str(item) for item in signal.get("warnings", [])]
    stability_warnings = [str(item) for item in stability.get("warnings", [])]
    failures.extend(signal_failures)
    failures.extend(stability_failures)
    warnings.extend(signal_warnings)
    warnings.extend(stability_warnings)

    if lighting_motion is not None:
        failures.extend(str(item) for item in lighting_motion.get("failures", []))
        warnings.extend(str(item) for item in lighting_motion.get("warnings", []))

    report_count = int(stability.get("report_count", 0) or 0)
    if report_count <= 0:
        failures.append("no frame reports were analyzed")
    if signal.get("ok_report_count") != report_count:
        failures.append(
            f"ok_report_count {signal.get('ok_report_count')} does not match report_count {report_count}"
        )
    if stability.get("default_beauty_affects_any") is not False:
        failures.append("V3 affected default beauty in a review packet")
    if stability.get("promoted_report_count") not in (0, None):
        failures.append("V3 report status was promoted before an explicit promotion decision")
    if stability.get("lighting_signal_metrics_ready") is not True:
        failures.append("lighting signal metrics are not ready")

    required_count_fields = {
        "material": "material_ready_report_count",
        "lighting": "lighting_split_ready_report_count",
        "environment": "scene_local_environment_ready_report_count",
        "reflection": "reflection_v3_ready_report_count",
        "composite": "composite_v3_ready_report_count",
        "cinematic_post": "cinematic_post_v3_ready_report_count",
    }
    domain_counts: dict[str, int] = {}
    for domain, field in required_count_fields.items():
        value = int(stability.get(field, 0) or 0)
        domain_counts[domain] = value
        if value != report_count:
            failures.append(f"{field} {value} does not match report_count {report_count}")

    ready_by_report = find_ready_domains(signal)
    for report, domains in ready_by_report.items():
        missing = sorted(REQUIRED_DOMAINS - domains)
        if missing:
            failures.append(f"{report} missing ready domains: {', '.join(missing)}")

    families = captured_families(manifest)
    missing_families = sorted(set(required_families) - set(families))
    if missing_families:
        message = "missing required families: " + ", ".join(missing_families)
        if allow_subset_review:
            warnings.append(message)
        else:
            failures.append(message)

    motion_mode = str(manifest.get("stability_motion_mode", "static"))
    observed_motion_modes = [motion_mode]
    if lighting_motion is not None:
        lm_mode = lighting_motion.get("motion_mode")
        if isinstance(lm_mode, str) and lm_mode and lm_mode not in observed_motion_modes:
            observed_motion_modes.append(lm_mode)
    missing_motion_modes = sorted(set(required_motion_modes) - set(observed_motion_modes))
    if missing_motion_modes:
        message = "missing required motion modes: " + ", ".join(missing_motion_modes)
        if allow_subset_review:
            warnings.append(message)
        else:
            failures.append(message)

    capture_sequence_count = int(manifest.get("capture_sequence_count", 0) or 0)
    if "mouse_jitter" in required_motion_modes or "camera_sweep" in required_motion_modes:
        if capture_sequence_count < 2:
            message = "motion promotion evidence requires capture_sequence_count >= 2"
            if allow_subset_review:
                warnings.append(message)
            else:
                failures.append(message)

    review_packet_passed = not failures
    full_coverage_ready = (
        review_packet_passed
        and not missing_families
        and not missing_motion_modes
        and capture_sequence_count >= 2
        and not warnings
    )
    if failures:
        status = "blocked"
    elif full_coverage_ready:
        status = "candidate_ready_not_promoted"
    else:
        status = "review_packet_passed"

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.promotion_decision.v1",
        "packet_root": str(packet_root),
        "status": status,
        "default_beauty_promotable": False,
        "review_packet_passed": review_packet_passed,
        "full_coverage_ready": full_coverage_ready,
        "report_count": report_count,
        "captured_families": families,
        "required_families": required_families,
        "missing_families": missing_families,
        "observed_motion_modes": observed_motion_modes,
        "required_motion_modes": required_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "capture_sequence_count": capture_sequence_count,
        "ready_domain_report_counts": domain_counts,
        "failures": failures,
        "warnings": warnings,
        "evidence": evidence,
    }


def write_markdown(path: pathlib.Path, decision: dict[str, Any]) -> None:
    lines: list[str] = [
        "# Full Scene Shader Pipeline V3 Promotion Decision",
        "",
        f"- packet root: `{decision.get('packet_root')}`",
        f"- status: `{decision.get('status')}`",
        f"- default beauty promotable: `{str(decision.get('default_beauty_promotable')).lower()}`",
        f"- review packet passed: `{str(decision.get('review_packet_passed')).lower()}`",
        f"- report count: `{decision.get('report_count', 0)}`",
        f"- captured families: `{','.join(decision.get('captured_families', []))}`",
        f"- missing families: `{','.join(decision.get('missing_families', []))}`",
        f"- observed motion modes: `{','.join(decision.get('observed_motion_modes', []))}`",
        f"- missing motion modes: `{','.join(decision.get('missing_motion_modes', []))}`",
        "",
        "| Domain | Ready Reports |",
        "|---|---:|",
    ]
    counts = decision.get("ready_domain_report_counts", {})
    if isinstance(counts, dict):
        for domain in sorted(counts):
            lines.append(f"| {domain} | {counts[domain]} |")
    if decision.get("failures"):
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in decision["failures"])
    if decision.get("warnings"):
        lines.extend(["", "## Warnings", ""])
        lines.extend(f"- {warning}" for warning in decision["warnings"])
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packet-root", required=True)
    parser.add_argument("--output-json", required=True)
    parser.add_argument("--output-md", required=True)
    parser.add_argument("--required-families", default=",".join(DEFAULT_REQUIRED_FAMILIES))
    parser.add_argument("--required-motion-modes", default=",".join(DEFAULT_REQUIRED_MOTION_MODES))
    parser.add_argument("--allow-subset-review", action="store_true")
    parser.add_argument("--fail-on-blocked", action="store_true")
    args = parser.parse_args()

    packet_root = pathlib.Path(args.packet_root)
    decision = make_decision(
        packet_root=packet_root,
        required_families=split_csv(args.required_families),
        required_motion_modes=split_csv(args.required_motion_modes),
        allow_subset_review=args.allow_subset_review,
    )

    output_json = pathlib.Path(args.output_json)
    output_md = pathlib.Path(args.output_md)
    output_json.parent.mkdir(parents=True, exist_ok=True)
    output_md.parent.mkdir(parents=True, exist_ok=True)
    output_json.write_text(json.dumps(decision, indent=2) + "\n", encoding="utf-8")
    write_markdown(output_md, decision)

    print(f"promotion_decision={output_json}")
    print(f"promotion_markdown={output_md}")
    if decision["failures"]:
        for failure in decision["failures"]:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1 if args.fail_on_blocked else 0
    print(f"PASS: V3 promotion decision status={decision['status']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
