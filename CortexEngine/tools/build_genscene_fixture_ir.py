#!/usr/bin/env python3
"""Build a deterministic generated-scene fixture IR without rendering it."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from director_ir_v3 import director_from_prompt, validate as validate_director_ir
from scene_compiler import compile_v3_to_v2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prompt", required=True)
    parser.add_argument("--ir-out", required=True, type=Path)
    parser.add_argument("--director-out", type=Path)
    args = parser.parse_args()

    director = director_from_prompt(args.prompt)
    errors = validate_director_ir(director)
    if errors:
        raise SystemExit("Director IR v3 validation failed: " + "; ".join(errors))

    ir = compile_v3_to_v2(director)
    args.ir_out.parent.mkdir(parents=True, exist_ok=True)
    args.ir_out.write_text(json.dumps(ir, indent=2), encoding="utf-8")
    if args.director_out:
        args.director_out.parent.mkdir(parents=True, exist_ok=True)
        args.director_out.write_text(json.dumps(director, indent=2), encoding="utf-8")

    print(json.dumps({
        "prompt": args.prompt,
        "ir": str(args.ir_out),
        "director": str(args.director_out) if args.director_out else "",
        "setting": ir.get("setting"),
        "objects": len(ir.get("objects", [])),
        "water": bool((ir.get("environment") or {}).get("water", {}).get("enabled")),
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
