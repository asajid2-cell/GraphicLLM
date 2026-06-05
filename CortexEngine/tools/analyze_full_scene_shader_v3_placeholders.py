#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import pathlib
import sys
from typing import Any


REQUIRED_OUTPUTS = {
    "material_attributes",
    "direct_lighting",
    "indirect_lighting",
    "shadow_visibility",
    "reflection_radiance",
    "reflection_confidence",
    "scene_local_environment",
    "hdr_scene_color",
    "ldr_cinematic_output",
}

REQUIRED_DOMAINS = {
    "render_graph",
    "material",
    "lighting",
    "reflection",
    "environment",
    "cinematic_post",
    "validation",
}

ALLOWED_READY_DOMAINS = {"material", "lighting"}

LIGHTING_SIGNAL_THRESHOLDS = {
    "direct_light": {"min_mean_luma": 0.02, "min_nonblack_ratio": 0.05},
    "direct_light_unshadowed": {"min_mean_luma": 0.02, "min_nonblack_ratio": 0.05},
    "direct_light_shadow_loss": {"min_mean_luma": 0.005, "min_nonblack_ratio": 0.01},
    "shadow_factor": {"min_mean_luma": 0.02, "max_mean_luma": 0.98, "min_nonblack_ratio": 0.05},
    "ambient_ibl": {"min_mean_luma": 0.01, "min_nonblack_ratio": 0.05},
    "v3_direct_lighting": {"min_mean_luma": 0.02, "min_nonblack_ratio": 0.05},
    "v3_direct_lighting_unshadowed": {"min_mean_luma": 0.02, "min_nonblack_ratio": 0.05},
    "v3_shadow_visibility": {"min_mean_luma": 0.02, "max_mean_luma": 0.98, "min_nonblack_ratio": 0.05},
    "v3_shadow_loss": {"min_mean_luma": 0.005, "min_nonblack_ratio": 0.01},
    "v3_indirect_lighting": {"min_mean_luma": 0.01, "min_nonblack_ratio": 0.05},
}


def load_json(path: pathlib.Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def find_debug_view_metrics(input_path: pathlib.Path) -> pathlib.Path | None:
    root = input_path if input_path.is_dir() else input_path.parent
    metrics_path = root / "debug_view_metrics.json"
    return metrics_path if metrics_path.exists() else None


def analyze_lighting_signal_metrics(input_path: pathlib.Path) -> dict[str, Any]:
    metrics_path = find_debug_view_metrics(input_path)
    failures: list[str] = []
    warnings: list[str] = []
    rows: list[dict[str, Any]] = []

    if metrics_path is None:
        return {
            "metrics_path": None,
            "ready": False,
            "failures": ["V3 lighting signal gate requires debug_view_metrics.json"],
            "warnings": warnings,
            "rows": rows,
        }

    metrics_report = load_json(metrics_path)
    if metrics_report.get("failures"):
        failures.extend(f"debug_view_metrics failure: {failure}" for failure in metrics_report["failures"])

    rows_by_view = {
        row.get("view"): row
        for row in metrics_report.get("rows", [])
        if isinstance(row, dict) and isinstance(row.get("view"), str)
    }

    for view, thresholds in LIGHTING_SIGNAL_THRESHOLDS.items():
        row = rows_by_view.get(view)
        if not isinstance(row, dict):
            failures.append(f"missing lighting debug-view metrics for {view}")
            rows.append({"view": view, "status": "missing"})
            continue
        metrics = row.get("metrics")
        if not isinstance(metrics, dict):
            failures.append(f"lighting debug-view metrics for {view} are malformed")
            rows.append({"view": view, "status": "malformed"})
            continue

        mean_luma = float(metrics.get("mean_luma", 0.0))
        nonblack_ratio = float(metrics.get("nonblack_ratio", 0.0))
        hot_pixel_ratio = float(metrics.get("hot_pixel_ratio", 0.0))
        max_luma = float(metrics.get("max_luma", 0.0))
        status = "ok"

        min_mean_luma = thresholds.get("min_mean_luma")
        if min_mean_luma is not None and mean_luma < min_mean_luma:
            failures.append(
                f"{view} mean_luma {mean_luma:.6f} below {min_mean_luma:.6f}"
            )
            status = "failed"
        max_mean_luma = thresholds.get("max_mean_luma")
        if max_mean_luma is not None and mean_luma > max_mean_luma:
            failures.append(
                f"{view} mean_luma {mean_luma:.6f} above {max_mean_luma:.6f}"
            )
            status = "failed"
        min_nonblack_ratio = thresholds.get("min_nonblack_ratio")
        if min_nonblack_ratio is not None and nonblack_ratio < min_nonblack_ratio:
            failures.append(
                f"{view} nonblack_ratio {nonblack_ratio:.6f} below {min_nonblack_ratio:.6f}"
            )
            status = "failed"

        rows.append(
            {
                "view": view,
                "status": status,
                "mean_luma": mean_luma,
                "max_luma": max_luma,
                "nonblack_ratio": nonblack_ratio,
                "hot_pixel_ratio": hot_pixel_ratio,
            }
        )

    direct = rows_by_view.get("direct_light", {}).get("metrics", {})
    unshadowed = rows_by_view.get("direct_light_unshadowed", {}).get("metrics", {})
    shadow_loss = rows_by_view.get("direct_light_shadow_loss", {}).get("metrics", {})
    if isinstance(direct, dict) and isinstance(unshadowed, dict):
        direct_luma = float(direct.get("mean_luma", 0.0))
        unshadowed_luma = float(unshadowed.get("mean_luma", 0.0))
        if unshadowed_luma + 1e-6 < direct_luma * 0.90:
            failures.append(
                "direct_light_unshadowed mean_luma should be at least 90% of direct_light "
                f"({unshadowed_luma:.6f} vs {direct_luma:.6f})"
            )
    if isinstance(shadow_loss, dict) and isinstance(unshadowed, dict):
        loss_luma = float(shadow_loss.get("mean_luma", 0.0))
        unshadowed_luma = float(unshadowed.get("mean_luma", 0.0))
        if unshadowed_luma > 0.0 and loss_luma > unshadowed_luma * 1.25:
            warnings.append(
                "direct_light_shadow_loss mean_luma is unusually high relative to unshadowed "
                f"({loss_luma:.6f} vs {unshadowed_luma:.6f})"
            )

    v3_direct = rows_by_view.get("v3_direct_lighting", {}).get("metrics", {})
    v3_unshadowed = rows_by_view.get("v3_direct_lighting_unshadowed", {}).get("metrics", {})
    v3_shadow_loss = rows_by_view.get("v3_shadow_loss", {}).get("metrics", {})
    if isinstance(v3_direct, dict) and isinstance(v3_unshadowed, dict):
        direct_luma = float(v3_direct.get("mean_luma", 0.0))
        unshadowed_luma = float(v3_unshadowed.get("mean_luma", 0.0))
        if unshadowed_luma + 1e-6 < direct_luma * 0.90:
            failures.append(
                "v3_direct_lighting_unshadowed mean_luma should be at least 90% of "
                f"v3_direct_lighting ({unshadowed_luma:.6f} vs {direct_luma:.6f})"
            )
    if isinstance(v3_shadow_loss, dict) and isinstance(v3_unshadowed, dict):
        loss_luma = float(v3_shadow_loss.get("mean_luma", 0.0))
        unshadowed_luma = float(v3_unshadowed.get("mean_luma", 0.0))
        if unshadowed_luma > 0.0 and loss_luma > unshadowed_luma * 1.25:
            warnings.append(
                "v3_shadow_loss mean_luma is unusually high relative to v3 unshadowed "
                f"({loss_luma:.6f} vs {unshadowed_luma:.6f})"
            )

    legacy_pairs = {
        "direct_light": "v3_direct_lighting",
        "direct_light_unshadowed": "v3_direct_lighting_unshadowed",
        "direct_light_shadow_loss": "v3_shadow_loss",
        "shadow_factor": "v3_shadow_visibility",
        "ambient_ibl": "v3_indirect_lighting",
    }
    for legacy_view, v3_view in legacy_pairs.items():
        legacy_metrics = rows_by_view.get(legacy_view, {}).get("metrics", {})
        v3_metrics = rows_by_view.get(v3_view, {}).get("metrics", {})
        if not isinstance(legacy_metrics, dict) or not isinstance(v3_metrics, dict):
            continue
        legacy_luma = float(legacy_metrics.get("mean_luma", 0.0))
        v3_luma = float(v3_metrics.get("mean_luma", 0.0))
        if legacy_luma <= 0.0:
            continue
        ratio = v3_luma / legacy_luma
        if ratio < 0.20 or ratio > 5.00:
            warnings.append(
                f"{v3_view} mean_luma is far from legacy {legacy_view}: "
                f"ratio={ratio:.3f} legacy={legacy_luma:.6f} v3={v3_luma:.6f}"
            )

    return {
        "metrics_path": str(metrics_path),
        "ready": not failures,
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def find_reports(root: pathlib.Path) -> list[pathlib.Path]:
    if root.is_file():
        return [root]
    return sorted(root.glob("**/frame_report_shutdown.json"))


def get_v3(report: dict[str, Any]) -> dict[str, Any] | None:
    frame_contract = report.get("frame_contract")
    if not isinstance(frame_contract, dict):
        return None
    v3 = frame_contract.get("full_scene_shader_pipeline_v3")
    return v3 if isinstance(v3, dict) else None


def find_frame_pass(report: dict[str, Any], name: str) -> dict[str, Any] | None:
    frame_contract = report.get("frame_contract")
    if not isinstance(frame_contract, dict):
        return None
    passes = frame_contract.get("passes", [])
    if not isinstance(passes, list):
        return None
    for frame_pass in passes:
        if isinstance(frame_pass, dict) and frame_pass.get("name") == name:
            return frame_pass
    return None


def analyze_report(
    path: pathlib.Path,
    *,
    require_lighting_split_ready: bool,
    require_lighting_split_draw_count: int | None,
) -> dict[str, Any]:
    report = load_json(path)
    v3 = get_v3(report)
    failures: list[str] = []
    warnings: list[str] = []

    if v3 is None:
        return {
            "report": str(path),
            "status": "missing_v3_report",
            "failures": ["frame_contract.full_scene_shader_pipeline_v3 is missing"],
            "warnings": [],
        }

    domains = v3.get("domains", [])
    domain_ids = {
        domain.get("id")
        for domain in domains
        if isinstance(domain, dict) and isinstance(domain.get("id"), str)
    }
    ready_domains = sorted(
        domain.get("id")
        for domain in domains
        if isinstance(domain, dict) and domain.get("ready") is True
    )
    domain_by_id = {
        domain.get("id"): domain
        for domain in domains
        if isinstance(domain, dict) and isinstance(domain.get("id"), str)
    }
    outputs = set(v3.get("required_outputs", []))

    if v3.get("schema") != "cortex.full_scene_shader_pipeline_v3.runtime_report.v1":
        failures.append("wrong V3 runtime report schema")
    if v3.get("status") != "planned_not_promoted":
        failures.append("V3 status must remain planned_not_promoted")
    if v3.get("default_beauty_affects") is not False:
        failures.append("V3 must not affect default beauty in placeholder mode")
    if v3.get("runtime_placeholders_ready") is not True:
        failures.append("runtime_placeholders_ready must be true")
    if v3.get("contract_grounded") is not True:
        failures.append("contract_grounded must be true")
    if v3.get("packet_gate_ready") is not False:
        failures.append("packet_gate_ready must remain false until V3 packets are real gates")

    missing_outputs = sorted(REQUIRED_OUTPUTS - outputs)
    if missing_outputs:
        failures.append("missing required outputs: " + ", ".join(missing_outputs))

    missing_domains = sorted(REQUIRED_DOMAINS - domain_ids)
    if missing_domains:
        failures.append("missing required domains: " + ", ".join(missing_domains))

    unexpected_ready_domains = sorted(set(ready_domains) - ALLOWED_READY_DOMAINS)
    if unexpected_ready_domains:
        warnings.append(
            "V3 has domains ready before their implementation gate: "
            + ", ".join(unexpected_ready_domains)
        )

    material_domain = domain_by_id.get("material")
    material_ready = isinstance(material_domain, dict) and material_domain.get("ready") is True
    if material_ready:
        if material_domain.get("output_resource") != "material_attributes":
            failures.append("material domain must output material_attributes")
        if material_domain.get("producer") != "FullSceneMaterialResolveV3":
            failures.append("material domain must be produced by FullSceneMaterialResolveV3")
        if material_domain.get("default_beauty_affects") is not False:
            failures.append("material domain must not affect default beauty yet")
        if material_domain.get("backing_resource_count", 0) < 6:
            failures.append("material domain ready without all backing resources")
        if material_domain.get("ready_channel_count", 0) < 14:
            failures.append("material domain ready without enough material channels")

    lighting_domain = domain_by_id.get("lighting")
    lighting_adapter_ready = v3.get("lighting_adapter_ready") is True
    lighting_split_pass = find_frame_pass(report, "FullSceneLightingV3")
    if lighting_adapter_ready:
        if not isinstance(lighting_domain, dict):
            failures.append("lighting adapter ready but lighting domain is missing")
        else:
            if v3.get("lighting_split_resources_ready") is True:
                if lighting_domain.get("producer") != "FullSceneLightingV3":
                    failures.append("ready lighting domain must be produced by FullSceneLightingV3")
                if lighting_domain.get("output_resource") != "lighting_split":
                    failures.append("ready lighting domain must expose lighting_split as its output")
                if lighting_domain.get("promotion_state") != "producer":
                    failures.append("ready lighting domain must be in producer promotion_state")
            else:
                if lighting_domain.get("producer") != "FullSceneLightingV3Adapter":
                    failures.append("lighting adapter must be produced by FullSceneLightingV3Adapter")
                if lighting_domain.get("output_resource") != "hdr_color":
                    failures.append("lighting adapter must honestly name hdr_color as current output")
            if v3.get("lighting_split_resources_allocated") is not True:
                failures.append("lighting adapter packet must expose allocated split lighting resources")
            if lighting_domain.get("ready") is True and v3.get("lighting_split_resources_ready") is not True:
                failures.append("lighting domain ready before split V3 lighting resources exist")
            if lighting_domain.get("default_beauty_affects") is not False:
                failures.append("lighting adapter must not mark default beauty as V3-owned")
            if lighting_domain.get("backing_resource_count", 0) < 1:
                failures.append("lighting adapter ready without hdr_color backing resource")
            if lighting_domain.get("ready_channel_count", 0) < 5:
                failures.append("lighting adapter ready without required debug signal channels")

    if require_lighting_split_ready:
        if v3.get("lighting_split_resources_ready") is not True:
            failures.append("V3 split packet requires lighting_split_resources_ready=true")
        if not isinstance(lighting_split_pass, dict):
            failures.append("V3 split packet requires FullSceneLightingV3 pass evidence")
        else:
            if lighting_split_pass.get("executed") is not True:
                failures.append("V3 split packet requires FullSceneLightingV3.executed=true")
            writes = set(lighting_split_pass.get("writes", []))
            missing_writes = sorted(
                {
                    "direct_lighting",
                    "direct_lighting_unshadowed",
                    "shadow_visibility",
                    "shadow_loss",
                    "indirect_lighting",
                }
                - writes
            )
            if missing_writes:
                failures.append(
                    "FullSceneLightingV3 missing split writes: " + ", ".join(missing_writes)
                )
            if (
                require_lighting_split_draw_count is not None
                and lighting_split_pass.get("draw_count") != require_lighting_split_draw_count
            ):
                failures.append(
                    "FullSceneLightingV3 draw_count must be "
                    f"{require_lighting_split_draw_count}, got "
                    f"{lighting_split_pass.get('draw_count')}"
                )

    return {
        "report": str(path),
        "status": "ok" if not failures else "failed",
        "schema": v3.get("schema"),
        "v3_status": v3.get("status"),
        "beauty_output": v3.get("beauty_output"),
        "default_beauty_affects": v3.get("default_beauty_affects"),
        "runtime_placeholders_ready": v3.get("runtime_placeholders_ready"),
        "contract_grounded": v3.get("contract_grounded"),
        "packet_gate_ready": v3.get("packet_gate_ready"),
        "required_output_count": len(outputs),
        "domain_count": len(domain_ids),
        "ready_domains": ready_domains,
        "material_attributes_ready": v3.get("material_attributes_ready"),
        "material_attributes_resource_count": v3.get("material_attributes_resource_count"),
        "material_attributes_channel_count": v3.get("material_attributes_channel_count"),
        "lighting_adapter_ready": v3.get("lighting_adapter_ready"),
        "lighting_split_resources_allocated": v3.get("lighting_split_resources_allocated"),
        "lighting_split_resources_ready": v3.get("lighting_split_resources_ready"),
        "lighting_adapter_signal_count": v3.get("lighting_adapter_signal_count"),
        "lighting_split_resource_count": v3.get("lighting_split_resource_count"),
        "full_scene_lighting_v3_executed": (
            lighting_split_pass.get("executed") if isinstance(lighting_split_pass, dict) else None
        ),
        "full_scene_lighting_v3_draw_count": (
            lighting_split_pass.get("draw_count") if isinstance(lighting_split_pass, dict) else None
        ),
        "failures": failures,
        "warnings": warnings,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="Frame report file or capture root")
    parser.add_argument("--signal-output", required=True)
    parser.add_argument("--stability-output", required=True)
    parser.add_argument("--require-lighting-split-ready", action="store_true")
    parser.add_argument("--require-lighting-split-draw-count", type=int)
    parser.add_argument("--require-lighting-signal-metrics", action="store_true")
    args = parser.parse_args()

    input_path = pathlib.Path(args.input)
    reports = find_reports(input_path)
    rows = [
        analyze_report(
            path,
            require_lighting_split_ready=args.require_lighting_split_ready,
            require_lighting_split_draw_count=args.require_lighting_split_draw_count,
        )
        for path in reports
    ]
    failures = [failure for row in rows for failure in row.get("failures", [])]
    warnings = [warning for row in rows for warning in row.get("warnings", [])]
    lighting_signal_metrics = None
    if args.require_lighting_signal_metrics:
        lighting_signal_metrics = analyze_lighting_signal_metrics(input_path)
        failures.extend(lighting_signal_metrics["failures"])
        warnings.extend(lighting_signal_metrics["warnings"])

    signal = {
        "schema": "cortex.full_scene_shader_pipeline_v3.placeholder_signal.v1",
        "input": str(input_path),
        "report_count": len(reports),
        "ok_report_count": sum(1 for row in rows if row.get("status") == "ok"),
        "lighting_signal_metrics": lighting_signal_metrics,
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }

    stability = {
        "schema": "cortex.full_scene_shader_pipeline_v3.placeholder_stability.v1",
        "input": str(input_path),
        "report_count": len(reports),
        "default_beauty_affects_any": any(
            row.get("default_beauty_affects") is not False for row in rows
        ),
        "promoted_report_count": sum(
            1 for row in rows if row.get("v3_status") != "planned_not_promoted"
        ),
        "ready_domain_report_count": sum(1 for row in rows if row.get("ready_domains")),
        "material_ready_report_count": sum(
            1 for row in rows if row.get("material_attributes_ready") is True
        ),
        "lighting_adapter_ready_report_count": sum(
            1 for row in rows if row.get("lighting_adapter_ready") is True
        ),
        "lighting_split_allocated_report_count": sum(
            1 for row in rows if row.get("lighting_split_resources_allocated") is True
        ),
        "lighting_split_ready_report_count": sum(
            1 for row in rows if row.get("lighting_split_resources_ready") is True
        ),
        "full_scene_lighting_v3_executed_report_count": sum(
            1 for row in rows if row.get("full_scene_lighting_v3_executed") is True
        ),
        "lighting_signal_metrics_ready": (
            lighting_signal_metrics.get("ready") if isinstance(lighting_signal_metrics, dict) else None
        ),
        "failures": failures,
        "warnings": warnings,
    }

    signal_path = pathlib.Path(args.signal_output)
    stability_path = pathlib.Path(args.stability_output)
    signal_path.parent.mkdir(parents=True, exist_ok=True)
    stability_path.parent.mkdir(parents=True, exist_ok=True)
    signal_path.write_text(json.dumps(signal, indent=2) + "\n", encoding="utf-8")
    stability_path.write_text(json.dumps(stability, indent=2) + "\n", encoding="utf-8")

    if failures:
        for failure in failures:
            print(f"ERROR: {failure}", file=sys.stderr)
        return 1
    print("PASS: Full Scene Shader Pipeline V3 placeholder packet artifacts are coherent")
    print(f"reports={len(reports)}")
    print(f"signal={signal_path}")
    print(f"stability={stability_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
