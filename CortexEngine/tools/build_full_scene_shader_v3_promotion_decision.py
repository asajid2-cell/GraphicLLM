#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


REQUIRED_DOMAINS = {
    "scene_profile",
    "material",
    "lighting",
    "environment",
    "reflection",
    "composite",
    "cinematic_post",
}
CANDIDATE_ONLY_DOMAINS = {
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

MATERIAL_QUALITY_THRESHOLDS = {
    "min_named_material_ratio": 0.95,
    "min_advanced_feature_ratio": 0.20,
    "min_reflection_eligible_ratio": 0.05,
    "max_contract_debug_view_debt": 0,
    "max_unresolved_default_fallback": 0,
    "min_missing_channel_mask_nonblack": 0.001,
}


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


def count_signal_flag(signal: dict[str, Any], field: str) -> int:
    count = 0
    for row in signal.get("rows", []):
        if isinstance(row, dict) and row.get(field) is True:
            count += 1
    return count


def signal_rows_with_flag(signal: dict[str, Any], field: str) -> list[dict[str, Any]]:
    return [
        row
        for row in signal.get("rows", [])
        if isinstance(row, dict) and row.get(field) is True
    ]


def candidate_predicate_summary(signal: dict[str, Any]) -> dict[str, Any]:
    rows = [row for row in signal.get("rows", []) if isinstance(row, dict)]
    requested_rows = [row for row in rows if row.get("candidate_beauty_requested") is True]
    ready_rows = [row for row in rows if row.get("candidate_beauty_ready") is True]
    blocker_counts: dict[str, int] = {}
    min_ready_predicates: int | None = None
    max_ready_predicates = 0
    predicate_count = 0
    predicate_totals = {
        "composite_ready": 0,
        "cinematic_post_ready": 0,
        "ldr_output_ready": 0,
        "reads_candidate_hdr": 0,
        "legacy_bridge_rejected": 0,
        "default_beauty_unchanged": 0,
    }

    for row in rows:
        ready_predicates = int(row.get("candidate_beauty_ready_predicate_count", 0) or 0)
        row_predicate_count = int(row.get("candidate_beauty_predicate_count", 0) or 0)
        predicate_count = max(predicate_count, row_predicate_count)
        max_ready_predicates = max(max_ready_predicates, ready_predicates)
        min_ready_predicates = (
            ready_predicates
            if min_ready_predicates is None
            else min(min_ready_predicates, ready_predicates)
        )
        blockers = row.get("candidate_beauty_blockers", [])
        if isinstance(blockers, list):
            for blocker in blockers:
                blocker_counts[str(blocker)] = blocker_counts.get(str(blocker), 0) + 1
        if row.get("candidate_beauty_composite_ready") is True:
            predicate_totals["composite_ready"] += 1
        if row.get("candidate_beauty_cinematic_post_ready") is True:
            predicate_totals["cinematic_post_ready"] += 1
        if row.get("candidate_beauty_ldr_output_ready") is True:
            predicate_totals["ldr_output_ready"] += 1
        if row.get("candidate_beauty_reads_candidate_hdr") is True:
            predicate_totals["reads_candidate_hdr"] += 1
        if row.get("candidate_beauty_legacy_bridge_rejected") is True:
            predicate_totals["legacy_bridge_rejected"] += 1
        if row.get("candidate_beauty_default_beauty_unchanged") is True:
            predicate_totals["default_beauty_unchanged"] += 1

    requested_blocker_counts: dict[str, int] = {}
    for row in requested_rows:
        blockers = row.get("candidate_beauty_blockers", [])
        if isinstance(blockers, list):
            for blocker in blockers:
                requested_blocker_counts[str(blocker)] = (
                    requested_blocker_counts.get(str(blocker), 0) + 1
                )

    return {
        "report_count": len(rows),
        "requested_report_count": len(requested_rows),
        "ready_report_count": len(ready_rows),
        "predicate_count": predicate_count,
        "min_ready_predicate_count": min_ready_predicates or 0,
        "max_ready_predicate_count": max_ready_predicates,
        "predicate_ready_report_counts": predicate_totals,
        "blocker_counts": dict(sorted(blocker_counts.items())),
        "requested_blocker_counts": dict(sorted(requested_blocker_counts.items())),
    }


def candidate_beauty_review_gate(
    *,
    review_packet_passed: bool,
    candidate_beauty_requested_count: int,
    candidate_beauty_ready_count: int,
    candidate_predicates: dict[str, Any],
    material_quality: dict[str, Any],
    scene_local_resource_contract: dict[str, Any] | None,
    composite_diagnostics: dict[str, Any] | None,
) -> dict[str, Any]:
    blockers: list[str] = []
    requested_blockers = candidate_predicates.get("requested_blocker_counts", {})
    if not isinstance(requested_blockers, dict):
        requested_blockers = {}

    if not review_packet_passed:
        blockers.append("review_packet_not_passed")
    if candidate_beauty_requested_count <= 0:
        blockers.append("candidate_beauty_not_requested")
    if candidate_beauty_ready_count != candidate_beauty_requested_count:
        blockers.append("candidate_beauty_ready_count_mismatch")
    if requested_blockers:
        blockers.append("candidate_beauty_requested_blockers_present")
    if material_quality.get("ready") is not True:
        blockers.append("material_quality_gate_not_ready")
    if scene_local_resource_contract is None or scene_local_resource_contract.get("ready") is not True:
        blockers.append("scene_local_resource_contract_not_ready")
    if composite_diagnostics is None or composite_diagnostics.get("ready") is not True:
        blockers.append("composite_v3_not_ready")

    return {
        "ready": not blockers,
        "blockers": blockers,
        "requested_report_count": candidate_beauty_requested_count,
        "ready_report_count": candidate_beauty_ready_count,
        "requested_blocker_counts": dict(sorted(requested_blockers.items())),
    }


def material_quality_gate(material_payload: dict[str, Any] | None) -> dict[str, Any]:
    if material_payload is None:
        return {
            "ready": False,
            "score": 0.0,
            "blockers": ["missing_material_payload"],
            "warnings": [],
            "thresholds": MATERIAL_QUALITY_THRESHOLDS,
        }

    summary = material_payload.get("summary", {})
    if not isinstance(summary, dict):
        summary = {}

    sampled = int(summary.get("sampled_materials_total", 0) or 0)
    named = int(summary.get("named_materials_total", 0) or 0)
    advanced = int(summary.get("advanced_feature_materials_total", 0) or 0)
    reflection_eligible = int(summary.get("reflection_eligible_total", 0) or 0)
    material_report_count = int(summary.get("material_report_count", 0) or 0)
    family_count = int(summary.get("family_count", 0) or 0)
    contract_debt = int(summary.get("contract_debug_view_debt_count", 0) or 0)
    unresolved_roughness = int(summary.get("unresolved_default_roughness_fallback_total", 0) or 0)
    unresolved_transmission = int(summary.get("unresolved_default_transmission_fallback_total", 0) or 0)
    missing_channel_mask_nonblack = float(
        summary.get("missing_channel_mask_nonblack_ratio_max", 0.0) or 0.0
    )

    named_ratio = named / sampled if sampled else 0.0
    advanced_ratio = advanced / sampled if sampled else 0.0
    reflection_eligible_ratio = reflection_eligible / sampled if sampled else 0.0

    blockers: list[str] = []
    warnings: list[str] = []
    predicates = {
        "material_payload_ready": material_payload.get("ready") is True,
        "material_reports_present": material_report_count > 0,
        "families_present": family_count > 0,
        "sampled_materials_present": sampled > 0,
        "named_material_ratio_ok": named_ratio >= MATERIAL_QUALITY_THRESHOLDS["min_named_material_ratio"],
        "advanced_feature_ratio_ok": advanced_ratio >= MATERIAL_QUALITY_THRESHOLDS["min_advanced_feature_ratio"],
        "reflection_eligible_ratio_ok": (
            reflection_eligible_ratio >= MATERIAL_QUALITY_THRESHOLDS["min_reflection_eligible_ratio"]
        ),
        "contract_debug_views_complete": contract_debt <= MATERIAL_QUALITY_THRESHOLDS["max_contract_debug_view_debt"],
        "unresolved_default_fallback_clear": (
            unresolved_roughness + unresolved_transmission
            <= MATERIAL_QUALITY_THRESHOLDS["max_unresolved_default_fallback"]
        ),
        "missing_channel_mask_debug_present": (
            missing_channel_mask_nonblack >= MATERIAL_QUALITY_THRESHOLDS["min_missing_channel_mask_nonblack"]
        ),
    }

    for predicate, passed in predicates.items():
        if not passed:
            blockers.append(predicate)

    class_roughness = int(summary.get("class_authored_default_roughness_total", 0) or 0)
    class_transmission = int(summary.get("class_authored_default_transmission_total", 0) or 0)
    if class_roughness or class_transmission:
        warnings.append(
            "material class-authored defaults still present: "
            f"roughness={class_roughness} transmission={class_transmission}"
        )

    score = sum(1 for passed in predicates.values() if passed) / max(1, len(predicates))
    return {
        "ready": not blockers,
        "score": score,
        "blockers": blockers,
        "warnings": warnings,
        "thresholds": MATERIAL_QUALITY_THRESHOLDS,
        "predicate_count": len(predicates),
        "ready_predicate_count": sum(1 for passed in predicates.values() if passed),
        "predicates": predicates,
        "summary": {
            "family_count": family_count,
            "material_report_count": material_report_count,
            "sampled_materials_total": sampled,
            "named_materials_total": named,
            "advanced_feature_materials_total": advanced,
            "reflection_eligible_total": reflection_eligible,
            "named_material_ratio": named_ratio,
            "advanced_feature_ratio": advanced_ratio,
            "reflection_eligible_ratio": reflection_eligible_ratio,
            "contract_debug_view_debt_count": contract_debt,
            "unresolved_default_roughness_fallback_total": unresolved_roughness,
            "unresolved_default_transmission_fallback_total": unresolved_transmission,
            "missing_channel_mask_nonblack_ratio_max": missing_channel_mask_nonblack,
        },
    }


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
    scene_profile_path = packet_root / "v3_scene_profile.json"
    environment_payload_path = packet_root / "v3_environment_payload.json"
    scene_local_resource_contract_path = packet_root / "scene_local_resource_contract_v1.json"
    material_payload_path = packet_root / "v3_material_payload.json"
    composite_diagnostics_path = packet_root / "v3_composite_diagnostics.json"

    failures: list[str] = []
    warnings: list[str] = []
    evidence: dict[str, Any] = {
        "manifest": str(manifest_path),
        "signal": str(signal_path),
        "stability": str(stability_path),
        "lighting_motion": str(lighting_motion_path) if lighting_motion_path.exists() else None,
        "scene_profile": str(scene_profile_path) if scene_profile_path.exists() else None,
        "environment_payload": str(environment_payload_path) if environment_payload_path.exists() else None,
        "scene_local_resource_contract": (
            str(scene_local_resource_contract_path) if scene_local_resource_contract_path.exists() else None
        ),
        "material_payload": str(material_payload_path) if material_payload_path.exists() else None,
        "composite_diagnostics": (
            str(composite_diagnostics_path) if composite_diagnostics_path.exists() else None
        ),
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
    scene_profile = load_json(scene_profile_path) if scene_profile_path.exists() else None
    environment_payload = load_json(environment_payload_path) if environment_payload_path.exists() else None
    scene_local_resource_contract = (
        load_json(scene_local_resource_contract_path)
        if scene_local_resource_contract_path.exists()
        else None
    )
    material_payload = load_json(material_payload_path) if material_payload_path.exists() else None
    composite_diagnostics = (
        load_json(composite_diagnostics_path) if composite_diagnostics_path.exists() else None
    )

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

    if material_payload is None:
        failures.append("missing v3_material_payload.json")
    else:
        failures.extend(str(item) for item in material_payload.get("failures", []))
        warnings.extend(str(item) for item in material_payload.get("warnings", []))
    material_quality = material_quality_gate(material_payload)
    if material_quality.get("ready") is not True:
        blockers = material_quality.get("blockers", [])
        blocker_text = ", ".join(str(item) for item in blockers) if isinstance(blockers, list) else "unknown"
        failures.append(f"Material quality gate is not ready: {blocker_text}")
    warnings.extend(str(item) for item in material_quality.get("warnings", []))
    if scene_profile is None:
        failures.append("missing v3_scene_profile.json")
    else:
        failures.extend(str(item) for item in scene_profile.get("failures", []))
        warnings.extend(str(item) for item in scene_profile.get("warnings", []))
    if environment_payload is None:
        failures.append("missing v3_environment_payload.json")
    else:
        failures.extend(str(item) for item in environment_payload.get("failures", []))
    scene_local_resource_summary: dict[str, Any] = {}
    if scene_local_resource_contract is None:
        failures.append("missing scene_local_resource_contract_v1.json")
    else:
        failures.extend(str(item) for item in scene_local_resource_contract.get("failures", []))
        warnings.extend(str(item) for item in scene_local_resource_contract.get("warnings", []))
        raw_summary = scene_local_resource_contract.get("summary", {})
        if isinstance(raw_summary, dict):
            scene_local_resource_summary = raw_summary
        if scene_local_resource_contract.get("ready") is not True:
            failures.append("Scene-local resource contract is not ready")
    composite_summary: dict[str, Any] = {}
    if composite_diagnostics is None:
        failures.append("missing v3_composite_diagnostics.json")
    else:
        failures.extend(str(item) for item in composite_diagnostics.get("failures", []))
        warnings.extend(str(item) for item in composite_diagnostics.get("warnings", []))
        raw_summary = composite_diagnostics.get("summary", {})
        if isinstance(raw_summary, dict):
            composite_summary = raw_summary
        if composite_diagnostics.get("ready") is not True:
            failures.append("CompositeV3 diagnostics are not ready")

    report_count = int(stability.get("report_count", 0) or 0)
    full_pipeline_report_count = int(stability.get("full_pipeline_report_count", report_count) or 0)
    material_payload_report_count = int(stability.get("material_payload_report_count", 0) or 0)
    if report_count <= 0:
        failures.append("no frame reports were analyzed")
    if signal.get("ok_report_count") != report_count:
        failures.append(
            f"ok_report_count {signal.get('ok_report_count')} does not match report_count {report_count}"
        )
    if full_pipeline_report_count <= 0:
        failures.append("no full-pipeline V3 reports were analyzed")
    if stability.get("default_beauty_affects_any") is not False:
        failures.append("V3 affected default beauty in a review packet")
    if stability.get("promoted_report_count") not in (0, None):
        failures.append("V3 report status was promoted before an explicit promotion decision")
    if stability.get("lighting_signal_metrics_ready") is not True:
        failures.append("lighting signal metrics are not ready")

    candidate_beauty_requested_count = count_signal_flag(signal, "candidate_beauty_requested")
    candidate_beauty_ready_count = count_signal_flag(signal, "candidate_beauty_ready")
    candidate_predicates = candidate_predicate_summary(signal)
    required_count_fields = {
        "scene_profile": "scene_profile_ready_report_count",
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
        if domain in CANDIDATE_ONLY_DOMAINS:
            expected_count = candidate_beauty_requested_count
            expected_label = "candidate_beauty_requested_count"
        elif domain == "material":
            expected_count = report_count
            expected_label = "report_count"
        else:
            expected_count = full_pipeline_report_count
            expected_label = "full_pipeline_report_count"
        if value != expected_count:
            failures.append(f"{field} {value} does not match {expected_label} {expected_count}")

    for row in signal.get("rows", []):
        if not isinstance(row, dict):
            continue
        report = str(row.get("report", ""))
        domains = {str(domain) for domain in row.get("ready_domains", []) if isinstance(domain, str)}
        if row.get("diagnostic_scope") == "material_payload":
            if "material" not in domains:
                failures.append(f"{report} material diagnostic row is missing material ready domain")
            continue
        required_domains = set(REQUIRED_DOMAINS)
        if row.get("candidate_beauty_requested") is not True:
            required_domains -= CANDIDATE_ONLY_DOMAINS
        missing = sorted(required_domains - domains)
        if missing:
            failures.append(f"{report} missing ready domains: {', '.join(missing)}")

    if candidate_beauty_ready_count > candidate_beauty_requested_count:
        failures.append("candidate beauty ready count exceeds requested count")
    if candidate_predicates["requested_report_count"] != candidate_beauty_requested_count:
        failures.append("candidate predicate requested count disagrees with signal count")
    if candidate_predicates["ready_report_count"] != candidate_beauty_ready_count:
        failures.append("candidate predicate ready count disagrees with signal count")
    if candidate_beauty_ready_count < candidate_beauty_requested_count:
        requested_blockers = candidate_predicates.get("requested_blocker_counts", {})
        if isinstance(requested_blockers, dict) and requested_blockers:
            warnings.append(
                "candidate beauty requested reports have blockers: "
                + ", ".join(f"{key}={value}" for key, value in requested_blockers.items())
            )
    for row in signal_rows_with_flag(signal, "candidate_beauty_ready"):
        report = str(row.get("report", "unknown_report"))
        if row.get("candidate_beauty_producer") != "CinematicPostV3":
            failures.append(f"{report} candidate beauty ready without CinematicPostV3 producer")
        if row.get("candidate_beauty_output") != "candidate_ldr_cinematic_output":
            failures.append(f"{report} candidate beauty ready without candidate_ldr_cinematic_output")

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

    mean_explicit_legacy_rescue = float(
        composite_summary.get("mean_explicit_legacy_rescue", 0.0) or 0.0
    )
    mean_legacy_rescue = float(composite_summary.get("mean_legacy_rescue", 0.0) or 0.0)
    mean_clamp_mask = float(composite_summary.get("mean_clamp_mask", 0.0) or 0.0)
    mean_clamp_ratio = float(composite_summary.get("mean_clamp_ratio", 0.0) or 0.0)
    mean_direct_contribution = float(composite_summary.get("mean_direct_contribution", 0.0) or 0.0)
    mean_reflection_contribution = float(composite_summary.get("mean_reflection_contribution", 0.0) or 0.0)
    if mean_explicit_legacy_rescue > 0.05:
        failures.append(
            "CompositeV3 explicit legacy rescue debt exceeds promotion gate: "
            f"{mean_explicit_legacy_rescue:.6f}"
        )
    if mean_legacy_rescue > 0.05:
        failures.append(
            "CompositeV3 overbright legacy rescue debt exceeds promotion gate: "
            f"{mean_legacy_rescue:.6f}"
        )
    if mean_clamp_mask > 0.10 or mean_clamp_ratio > 0.10:
        failures.append(
            "CompositeV3 clamp debt exceeds promotion gate: "
            f"mask={mean_clamp_mask:.6f} ratio={mean_clamp_ratio:.6f}"
        )
    if (
        candidate_beauty_requested_count > 0
        and mean_direct_contribution < 0.001
        and mean_reflection_contribution < 0.001
    ):
        failures.append("CompositeV3 contribution map lacks owned direct/reflection contribution")

    review_packet_passed = not failures
    candidate_beauty_review = candidate_beauty_review_gate(
        review_packet_passed=review_packet_passed,
        candidate_beauty_requested_count=candidate_beauty_requested_count,
        candidate_beauty_ready_count=candidate_beauty_ready_count,
        candidate_predicates=candidate_predicates,
        material_quality=material_quality,
        scene_local_resource_contract=scene_local_resource_contract,
        composite_diagnostics=composite_diagnostics,
    )
    full_coverage_ready = (
        review_packet_passed
        and not missing_families
        and not missing_motion_modes
        and capture_sequence_count >= 2
        and not warnings
    )
    default_beauty_promotion_blockers: list[str] = []
    if candidate_beauty_review.get("ready") is not True:
        default_beauty_promotion_blockers.append("candidate_beauty_review_not_ready")
    if not full_coverage_ready:
        default_beauty_promotion_blockers.append("full_coverage_not_ready")
    if stability.get("default_beauty_affects_any") is not False:
        default_beauty_promotion_blockers.append("default_beauty_review_invariant_failed")
    else:
        default_beauty_promotion_blockers.append("default_beauty_runtime_path_not_enabled")
    default_beauty_promotable = not default_beauty_promotion_blockers

    if failures:
        status = "blocked"
    elif default_beauty_promotable:
        status = "default_beauty_promotable"
    elif full_coverage_ready and candidate_beauty_review.get("ready") is True:
        status = "candidate_ready_not_promoted"
    else:
        status = "review_packet_passed"

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.promotion_decision.v1",
        "packet_root": str(packet_root),
        "status": status,
        "default_beauty_promotable": default_beauty_promotable,
        "default_beauty_promotion_blockers": default_beauty_promotion_blockers,
        "candidate_beauty_review_ready": candidate_beauty_review.get("ready") is True,
        "candidate_beauty_review_gate": candidate_beauty_review,
        "review_packet_passed": review_packet_passed,
        "full_coverage_ready": full_coverage_ready,
        "report_count": report_count,
        "full_pipeline_report_count": full_pipeline_report_count,
        "material_payload_report_count": material_payload_report_count,
        "captured_families": families,
        "required_families": required_families,
        "missing_families": missing_families,
        "observed_motion_modes": observed_motion_modes,
        "required_motion_modes": required_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "capture_sequence_count": capture_sequence_count,
        "ready_domain_report_counts": domain_counts,
        "candidate_beauty_requested_report_count": candidate_beauty_requested_count,
        "candidate_beauty_ready_report_count": candidate_beauty_ready_count,
        "candidate_beauty_predicates": candidate_predicates,
        "material_quality_gate": material_quality,
        "composite_v3_diagnostics": {
            "mean_explicit_legacy_rescue": mean_explicit_legacy_rescue,
            "mean_legacy_rescue": mean_legacy_rescue,
            "mean_clamp_mask": mean_clamp_mask,
            "mean_clamp_ratio": mean_clamp_ratio,
            "mean_direct_contribution": mean_direct_contribution,
            "mean_reflection_contribution": mean_reflection_contribution,
        },
        "scene_local_resource_contract": {
            "report_count": int(scene_local_resource_summary.get("report_count", 0) or 0),
            "ready_report_count": int(scene_local_resource_summary.get("ready_report_count", 0) or 0),
            "contract_families": scene_local_resource_summary.get("contract_families", []),
            "proved_role_counts": scene_local_resource_summary.get("proved_role_counts", {}),
        },
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
        f"- candidate beauty review ready: `{str(decision.get('candidate_beauty_review_ready')).lower()}`",
        f"- review packet passed: `{str(decision.get('review_packet_passed')).lower()}`",
        f"- report count: `{decision.get('report_count', 0)}`",
        f"- full-pipeline reports: `{decision.get('full_pipeline_report_count', 0)}`",
        f"- material-payload reports: `{decision.get('material_payload_report_count', 0)}`",
        f"- candidate beauty requested reports: `{decision.get('candidate_beauty_requested_report_count', 0)}`",
        f"- candidate beauty ready reports: `{decision.get('candidate_beauty_ready_report_count', 0)}`",
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
    candidate_predicates = decision.get("candidate_beauty_predicates", {})
    if isinstance(candidate_predicates, dict):
        blocker_counts = candidate_predicates.get("blocker_counts", {})
        requested_blocker_counts = candidate_predicates.get("requested_blocker_counts", {})
        lines.extend(
            [
                "",
                "## Candidate Beauty Predicates",
                "",
                f"- predicate count: `{candidate_predicates.get('predicate_count', 0)}`",
                f"- min ready predicates: `{candidate_predicates.get('min_ready_predicate_count', 0)}`",
                f"- max ready predicates: `{candidate_predicates.get('max_ready_predicate_count', 0)}`",
                f"- blocker counts: `{json.dumps(blocker_counts, sort_keys=True)}`",
                f"- requested blocker counts: `{json.dumps(requested_blocker_counts, sort_keys=True)}`",
            ]
        )
    candidate_review = decision.get("candidate_beauty_review_gate", {})
    if isinstance(candidate_review, dict):
        lines.extend(
            [
                "",
                "## Candidate Beauty Review Gate",
                "",
                f"- ready: `{str(candidate_review.get('ready')).lower()}`",
                f"- blockers: `{json.dumps(candidate_review.get('blockers', []), sort_keys=True)}`",
                f"- requested reports: `{candidate_review.get('requested_report_count', 0)}`",
                f"- ready reports: `{candidate_review.get('ready_report_count', 0)}`",
                f"- requested blocker counts: `{json.dumps(candidate_review.get('requested_blocker_counts', {}), sort_keys=True)}`",
            ]
        )
    lines.extend(
        [
            "",
            "## Default Beauty Promotion Gate",
            "",
            f"- promotable: `{str(decision.get('default_beauty_promotable')).lower()}`",
            f"- blockers: `{json.dumps(decision.get('default_beauty_promotion_blockers', []), sort_keys=True)}`",
        ]
    )
    material_quality = decision.get("material_quality_gate", {})
    if isinstance(material_quality, dict):
        material_summary = material_quality.get("summary", {})
        if not isinstance(material_summary, dict):
            material_summary = {}
        lines.extend(
            [
                "",
                "## Material Quality Gate",
                "",
                f"- ready: `{str(material_quality.get('ready')).lower()}`",
                f"- score: `{float(material_quality.get('score', 0.0) or 0.0):.4f}`",
                f"- ready predicates: `{material_quality.get('ready_predicate_count', 0)}/{material_quality.get('predicate_count', 0)}`",
                f"- blockers: `{json.dumps(material_quality.get('blockers', []), sort_keys=True)}`",
                f"- sampled materials: `{material_summary.get('sampled_materials_total', 0)}`",
                f"- named material ratio: `{float(material_summary.get('named_material_ratio', 0.0) or 0.0):.4f}`",
                f"- advanced feature ratio: `{float(material_summary.get('advanced_feature_ratio', 0.0) or 0.0):.4f}`",
                f"- reflection eligible ratio: `{float(material_summary.get('reflection_eligible_ratio', 0.0) or 0.0):.4f}`",
                f"- contract debug-view debt: `{material_summary.get('contract_debug_view_debt_count', 0)}`",
                f"- unresolved roughness fallback: `{material_summary.get('unresolved_default_roughness_fallback_total', 0)}`",
                f"- unresolved transmission fallback: `{material_summary.get('unresolved_default_transmission_fallback_total', 0)}`",
                f"- missing-channel mask max nonblack: `{float(material_summary.get('missing_channel_mask_nonblack_ratio_max', 0.0) or 0.0):.4f}`",
            ]
        )
    composite = decision.get("composite_v3_diagnostics", {})
    if isinstance(composite, dict):
        lines.extend(
            [
                "",
                "## CompositeV3 Diagnostics",
                "",
                f"- mean explicit legacy rescue: `{float(composite.get('mean_explicit_legacy_rescue', 0.0) or 0.0):.6f}`",
                f"- mean legacy rescue: `{float(composite.get('mean_legacy_rescue', 0.0) or 0.0):.6f}`",
                f"- mean clamp mask: `{float(composite.get('mean_clamp_mask', 0.0) or 0.0):.6f}`",
                f"- mean clamp ratio: `{float(composite.get('mean_clamp_ratio', 0.0) or 0.0):.6f}`",
                f"- mean direct contribution: `{float(composite.get('mean_direct_contribution', 0.0) or 0.0):.6f}`",
                f"- mean reflection contribution: `{float(composite.get('mean_reflection_contribution', 0.0) or 0.0):.6f}`",
            ]
        )
    resource_contract = decision.get("scene_local_resource_contract", {})
    if isinstance(resource_contract, dict):
        lines.extend(
            [
                "",
                "## Scene-Local Resource Contract",
                "",
                f"- reports: `{resource_contract.get('report_count', 0)}`",
                f"- ready reports: `{resource_contract.get('ready_report_count', 0)}`",
                f"- contract families: `{', '.join(resource_contract.get('contract_families', []))}`",
                "",
                "| Role | Proved Reports |",
                "|---|---:|",
            ]
        )
        proved_role_counts = resource_contract.get("proved_role_counts", {})
        if isinstance(proved_role_counts, dict):
            for role in sorted(proved_role_counts):
                lines.append(f"| {role} | {proved_role_counts[role]} |")
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
