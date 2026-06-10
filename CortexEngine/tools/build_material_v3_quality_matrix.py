#!/usr/bin/env python3
"""Build a promotion-grade MaterialV3 quality matrix.

Tool marker: build_material_v3_quality_matrix.py.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from build_full_scene_shader_v3_promotion_decision import material_quality_gate


ROOT = Path(__file__).resolve().parents[1]


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


def manifest_families(manifest: dict[str, Any]) -> list[str]:
    families = {
        str(row.get("family", ""))
        for row in manifest.get("results", [])
        if isinstance(row, dict) and row.get("family")
    }
    return sorted(families)


def packet_row(packet_root: Path) -> dict[str, Any]:
    manifest_path = packet_root / "manifest.json"
    material_path = packet_root / "v3_material_payload.json"
    row: dict[str, Any] = {
        "packet_root": str(packet_root),
        "manifest": str(manifest_path),
        "material_payload": str(material_path),
        "exists": packet_root.exists(),
        "families": [],
        "motion_mode": "unknown",
        "material_payload_ready": False,
        "material_quality_ready": False,
        "material_quality_score": 0.0,
        "material_quality_blockers": [],
        "material_quality_warnings": [],
        "summary": {},
        "failures": [],
        "warnings": [],
    }
    if not packet_root.exists():
        row["failures"].append("packet root missing")
        return row
    if not manifest_path.exists():
        row["failures"].append("manifest.json missing")
        return row
    if not material_path.exists():
        row["failures"].append("v3_material_payload.json missing")
        return row

    manifest = load_json(manifest_path)
    material = load_json(material_path)
    quality = material_quality_gate(material)
    row["families"] = manifest_families(manifest)
    row["motion_mode"] = str(manifest.get("stability_motion_mode", "static"))
    row["material_payload_ready"] = material.get("ready") is True
    row["material_quality_ready"] = quality.get("ready") is True
    row["material_quality_score"] = float(quality.get("score", 0.0) or 0.0)
    row["material_quality_blockers"] = list(quality.get("blockers", []))
    row["material_quality_warnings"] = list(quality.get("warnings", []))
    row["summary"] = quality.get("summary", {})

    for failure in material.get("failures", []):
        row["failures"].append(f"material_payload: {failure}")
    for warning in material.get("warnings", []):
        row["warnings"].append(f"material_payload: {warning}")
    for blocker in row["material_quality_blockers"]:
        row["failures"].append(f"material_quality: {blocker}")
    for warning in row["material_quality_warnings"]:
        row["warnings"].append(f"material_quality: {warning}")

    return row


def build_matrix(
    packet_roots: list[Path],
    *,
    required_families: list[str],
    required_motion_modes: list[str],
    min_quality_score: float,
) -> dict[str, Any]:
    rows = [packet_row(root) for root in packet_roots]
    ready_rows = [
        row
        for row in rows
        if row.get("material_payload_ready") is True
        and row.get("material_quality_ready") is True
        and float(row.get("material_quality_score", 0.0) or 0.0) >= min_quality_score
        and not row.get("failures")
    ]

    observed_families = sorted(
        {
            family
            for row in ready_rows
            for family in row.get("families", [])
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
    blocker_counts: dict[str, int] = {}
    warning_counts: dict[str, int] = {}
    min_score = 1.0 if rows else 0.0
    for row in rows:
        min_score = min(min_score, float(row.get("material_quality_score", 0.0) or 0.0))
        for failure in row.get("failures", []):
            failures.append(f"{row.get('packet_root')}: {failure}")
        for warning in row.get("warnings", []):
            warnings.append(f"{row.get('packet_root')}: {warning}")
        for blocker in row.get("material_quality_blockers", []):
            key = str(blocker)
            blocker_counts[key] = blocker_counts.get(key, 0) + 1
        for warning in row.get("material_quality_warnings", []):
            key = str(warning)
            warning_counts[key] = warning_counts.get(key, 0) + 1

    missing_families = sorted(set(required_families) - set(observed_families))
    missing_motion_modes = sorted(set(required_motion_modes) - set(observed_motion_modes))
    if missing_families:
        failures.append("missing required material families: " + ", ".join(missing_families))
    if missing_motion_modes:
        failures.append("missing required material motion modes: " + ", ".join(missing_motion_modes))
    if min_score < min_quality_score:
        failures.append(f"minimum material quality score {min_score:.4f} below {min_quality_score:.4f}")

    totals = {
        "sampled_materials_total": sum(
            int(row.get("summary", {}).get("sampled_materials_total", 0) or 0) for row in rows
        ),
        "named_materials_total": sum(
            int(row.get("summary", {}).get("named_materials_total", 0) or 0) for row in rows
        ),
        "advanced_feature_materials_total": sum(
            int(row.get("summary", {}).get("advanced_feature_materials_total", 0) or 0)
            for row in rows
        ),
        "reflection_eligible_total": sum(
            int(row.get("summary", {}).get("reflection_eligible_total", 0) or 0) for row in rows
        ),
        "unresolved_default_roughness_fallback_total": sum(
            int(row.get("summary", {}).get("unresolved_default_roughness_fallback_total", 0) or 0)
            for row in rows
        ),
        "unresolved_default_transmission_fallback_total": sum(
            int(row.get("summary", {}).get("unresolved_default_transmission_fallback_total", 0) or 0)
            for row in rows
        ),
        "contract_debug_view_debt_count_total": sum(
            int(row.get("summary", {}).get("contract_debug_view_debt_count", 0) or 0) for row in rows
        ),
    }
    sampled = max(int(totals["sampled_materials_total"]), 1)
    totals["named_material_ratio"] = totals["named_materials_total"] / sampled
    totals["advanced_feature_ratio"] = totals["advanced_feature_materials_total"] / sampled
    totals["reflection_eligible_ratio"] = totals["reflection_eligible_total"] / sampled

    return {
        "schema": "cortex.full_scene_shader_pipeline_v3.material_quality_matrix.v1",
        "packet_count": len(rows),
        "ready_packet_count": len(ready_rows),
        "required_families": required_families,
        "required_motion_modes": required_motion_modes,
        "observed_families": observed_families,
        "missing_families": missing_families,
        "observed_motion_modes": observed_motion_modes,
        "missing_motion_modes": missing_motion_modes,
        "material_quality_ready": not failures,
        "minimum_material_quality_score": min_score,
        "required_min_quality_score": min_quality_score,
        "blocker_counts": dict(sorted(blocker_counts.items())),
        "warning_counts": dict(sorted(warning_counts.items())),
        "totals": totals,
        "failures": failures,
        "warnings": warnings,
        "rows": rows,
    }


def write_markdown(matrix: dict[str, Any], output: Path) -> None:
    totals = matrix["totals"]
    lines = [
        "# MaterialV3 Quality Matrix",
        "",
        f"- packet count: `{matrix['packet_count']}`",
        f"- ready packets: `{matrix['ready_packet_count']}`",
        f"- material quality ready: `{str(matrix['material_quality_ready']).lower()}`",
        f"- minimum quality score: `{matrix['minimum_material_quality_score']:.4f}`",
        f"- observed families: `{', '.join(matrix['observed_families'])}`",
        f"- missing families: `{', '.join(matrix['missing_families'])}`",
        f"- observed motion modes: `{', '.join(matrix['observed_motion_modes'])}`",
        f"- missing motion modes: `{', '.join(matrix['missing_motion_modes'])}`",
        f"- sampled materials: `{totals['sampled_materials_total']}`",
        f"- named material ratio: `{totals['named_material_ratio']:.4f}`",
        f"- advanced feature ratio: `{totals['advanced_feature_ratio']:.4f}`",
        f"- reflection eligible ratio: `{totals['reflection_eligible_ratio']:.4f}`",
        f"- contract debug-view debt total: `{totals['contract_debug_view_debt_count_total']}`",
        f"- unresolved roughness fallback total: `{totals['unresolved_default_roughness_fallback_total']}`",
        f"- unresolved transmission fallback total: `{totals['unresolved_default_transmission_fallback_total']}`",
        f"- failures: `{len(matrix['failures'])}`",
        f"- warnings: `{len(matrix['warnings'])}`",
        "",
        "| Packet | Motion | Families | Ready | Score | Sampled | Named Ratio | Advanced Ratio | Reflection Ratio | Blockers | Warnings |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in matrix["rows"]:
        summary = row.get("summary", {})
        sampled = int(summary.get("sampled_materials_total", 0) or 0)
        denom = max(sampled, 1)
        named_ratio = int(summary.get("named_materials_total", 0) or 0) / denom
        advanced_ratio = int(summary.get("advanced_feature_materials_total", 0) or 0) / denom
        reflection_ratio = int(summary.get("reflection_eligible_total", 0) or 0) / denom
        lines.append(
            "| {packet} | {motion} | {families} | {ready} | {score:.4f} | {sampled} | {named:.4f} | {advanced:.4f} | {reflection:.4f} | {blockers} | {warnings} |".format(
                packet=row["packet_root"],
                motion=row["motion_mode"],
                families=", ".join(row.get("families", [])),
                ready=str(bool(row.get("material_quality_ready"))).lower(),
                score=float(row.get("material_quality_score", 0.0) or 0.0),
                sampled=sampled,
                named=named_ratio,
                advanced=advanced_ratio,
                reflection=reflection_ratio,
                blockers=len(row.get("material_quality_blockers", [])),
                warnings=len(row.get("material_quality_warnings", [])),
            )
        )
    if matrix["blocker_counts"]:
        lines.extend(["", "## Blocker Counts", ""])
        lines.extend(f"- `{key}`: `{value}`" for key, value in matrix["blocker_counts"].items())
    if matrix["warning_counts"]:
        lines.extend(["", "## Warning Counts", ""])
        lines.extend(f"- `{key}`: `{value}`" for key, value in matrix["warning_counts"].items())
    if matrix["failures"]:
        lines.extend(["", "## Failures", ""])
        lines.extend(f"- {failure}" for failure in matrix["failures"])
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--packet-root", action="append", required=True)
    parser.add_argument(
        "--required-families",
        default="stress_rt_showcase_reflection_closeup,gallery,kitchen,office,gym,concert,red_room,stadium",
    )
    parser.add_argument("--required-motion-modes", default="static,mouse_jitter,camera_sweep,light_sweep")
    parser.add_argument("--min-quality-score", type=float, default=1.0)
    parser.add_argument("--output-json", required=True, type=Path)
    parser.add_argument("--output-md", required=True, type=Path)
    args = parser.parse_args()

    matrix = build_matrix(
        [normalize_packet_root(root) for root in args.packet_root],
        required_families=split_csv(args.required_families),
        required_motion_modes=split_csv(args.required_motion_modes),
        min_quality_score=args.min_quality_score,
    )

    args.output_json.parent.mkdir(parents=True, exist_ok=True)
    args.output_md.parent.mkdir(parents=True, exist_ok=True)
    args.output_json.write_text(json.dumps(matrix, indent=2) + "\n", encoding="utf-8")
    write_markdown(matrix, args.output_md)

    print(f"material_matrix={args.output_json}")
    print(f"material_matrix_md={args.output_md}")
    if not matrix["material_quality_ready"]:
        for failure in matrix["failures"]:
            print(f"ERROR: {failure}")
        return 1
    print("PASS: MaterialV3 quality matrix is fully covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
