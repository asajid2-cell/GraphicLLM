#!/usr/bin/env python3
"""Promote selected GenScene renders into the curated docs gallery.

This is a promotion tool, not a quality judge. It prevents loop artifacts and
machine codenames from leaking into public media by requiring a human-readable
`genscene_*` id and writing a manifest record for every promoted still.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

try:
    from PIL import Image, ImageOps
except Exception:  # pragma: no cover - checked at runtime
    Image = None
    ImageOps = None


ROOT = Path(__file__).resolve().parent.parent
GALLERY = ROOT / "docs" / "media" / "genscene"
DEFAULT_SIZE = (1920, 1080)
ID_RE = re.compile(r"^genscene_[a-z0-9]+(?:_[a-z0-9]+)*$")
FORBIDDEN_ID_TOKENS = {
    "aaa",
    "artifact",
    "build",
    "claude",
    "codex",
    "debug",
    "gen",
    "iter",
    "loop",
    "render",
    "test",
    "tmp",
}


def _utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def _rel(path: Path) -> str:
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def _git_commit() -> str:
    out = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    return out.stdout.strip() if out.returncode == 0 else ""


def _load_manifest(manifest_path: Path) -> dict[str, Any]:
    if not manifest_path.exists():
        return {"schema": 1, "generated_utc": _utc_now(), "entries": []}
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise SystemExit(f"manifest is not an object: {manifest_path}")
    data.setdefault("schema", 1)
    data.setdefault("entries", [])
    if not isinstance(data["entries"], list):
        raise SystemExit(f"manifest entries is not a list: {manifest_path}")
    return data


def _write_manifest(manifest_path: Path, data: dict[str, Any]) -> None:
    data["generated_utc"] = _utc_now()
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _validate_id(curated_id: str) -> None:
    if not ID_RE.match(curated_id):
        raise SystemExit("id must look like genscene_campsite_dawn_hero")
    tokens = set(curated_id.split("_"))
    bad = sorted(tokens & FORBIDDEN_ID_TOKENS)
    if bad:
        raise SystemExit(f"id contains non-public token(s): {', '.join(bad)}")


def _sidecars_for(source: Path) -> dict[str, str]:
    stem = source.with_suffix("")
    candidates = {
        "ir": stem.parent / f"{stem.name}_ir.json",
        "frame_report": stem.parent / f"{stem.name}_frame_report.json",
        "log": stem.parent / f"{stem.name}.out",
    }
    return {key: _rel(path) for key, path in candidates.items() if path.exists()}


def _promote_image(source: Path, dest: Path, size: tuple[int, int], force: bool) -> tuple[int, int]:
    if dest.exists() and not force:
        raise SystemExit(f"destination exists; use --force to replace: {dest}")
    if Image is None or ImageOps is None:
        raise SystemExit("Pillow is required to re-encode gallery images")
    with Image.open(source) as img:
        img = ImageOps.exif_transpose(img).convert("RGB")
        img = ImageOps.fit(img, size, method=Image.Resampling.LANCZOS)
        dest.parent.mkdir(parents=True, exist_ok=True)
        img.save(dest, format="PNG", optimize=True)
    return size


def _build_entry(args: argparse.Namespace, source: Path, dest: Path, width: int, height: int) -> dict[str, Any]:
    entry: dict[str, Any] = {
        "id": args.id,
        "title": args.title or args.id.removeprefix("genscene_").replace("_", " ").title(),
        "image": _rel(dest),
        "prompt": args.prompt,
        "source": _rel(source),
        "curated_utc": _utc_now(),
        "commit": _git_commit(),
        "width": width,
        "height": height,
    }
    if args.seed:
        entry["seed"] = args.seed
    if args.note:
        entry["note"] = args.note
    if args.settings:
        entry["settings"] = json.loads(args.settings)
    sidecars = _sidecars_for(source)
    if sidecars:
        entry["sidecars"] = sidecars
    return entry


def promote(args: argparse.Namespace) -> dict[str, Any]:
    _validate_id(args.id)
    source = Path(args.src)
    if not source.is_absolute():
        source = (ROOT / source).resolve()
    if not source.exists() or not source.is_file():
        raise SystemExit(f"source image does not exist: {source}")
    if source.suffix.lower() not in {".png", ".jpg", ".jpeg", ".bmp"}:
        raise SystemExit("source must be a PNG, JPG, or BMP image")
    if not args.prompt.strip():
        raise SystemExit("--prompt is required")

    width, height = args.size
    gallery = GALLERY / "tmp" if args.staging else GALLERY
    manifest_path = gallery / "manifest.json"
    dest = gallery / f"{args.id}.png"
    entry = _build_entry(args, source, dest, width, height)
    if args.dry_run:
        return {"dry_run": True, "entry": entry}

    _promote_image(source, dest, (width, height), args.force)
    manifest = _load_manifest(manifest_path)
    entries: list[dict[str, Any]] = manifest["entries"]
    if any(item.get("id") == args.id for item in entries):
        if not args.force:
            raise SystemExit(f"manifest already has id; use --force to replace: {args.id}")
        entries[:] = [item for item in entries if item.get("id") != args.id]
    entries.append(entry)
    entries.sort(key=lambda item: str(item.get("id", "")))
    _write_manifest(manifest_path, manifest)
    return {"dry_run": False, "entry": entry, "manifest": _rel(manifest_path)}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--src", required=True, help="source render path, usually build/bin/logs/<name>.png")
    parser.add_argument("--id", required=True, help="public id, e.g. genscene_campsite_dawn_hero")
    parser.add_argument("--prompt", required=True, help="source prompt for the render")
    parser.add_argument("--title", default="", help="human title; defaults from --id")
    parser.add_argument("--seed", default="", help="optional seed or generation id")
    parser.add_argument("--note", default="", help="optional curation note")
    parser.add_argument("--settings", default="", help="optional JSON object of capture/settings metadata")
    parser.add_argument("--size", nargs=2, type=int, default=DEFAULT_SIZE, metavar=("WIDTH", "HEIGHT"))
    parser.add_argument("--force", action="store_true", help="replace existing image/manifest entry")
    parser.add_argument("--staging", action="store_true", help="write under ignored docs/media/genscene/tmp")
    parser.add_argument("--dry-run", action="store_true", help="validate and print the manifest entry without writing")
    return parser.parse_args()


def main() -> int:
    result = promote(parse_args())
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
