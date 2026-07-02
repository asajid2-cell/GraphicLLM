#!/usr/bin/env python3
"""
bake_flat_gltf.py -- convert a .glb (or .gltf) whose materials are FLAT COLORS into
the engine loader's dialect: .gltf JSON + ONE external .bin + a tiny palette PNG.

Why: Utils::LoadGLTFMesh keeps a SINGLE material for the whole mesh (multi-material
models collapse -- a palm tree turns all-bark), reads only .gltf + external .bin
(no .glb, no data: URIs, no COLOR_0). Kenney packs colour models with several flat
baseColorFactor materials, so a straight format conversion loses the colours.

The bake: every flat material becomes a 64x64 block in a palette strip PNG; every
primitive gets a constant TEXCOORD_0 at its block's centre; all primitives share one
white-factor material that references the palette. The loader keeps that (first
textured) material and the colours survive per-face. Textured source materials are
left alone (their primitives keep original UVs + the first such texture wins).

Usage:
  python tools/bake_flat_gltf.py in.glb out_dir/name        -> out_dir/name/name.gltf (+.bin +_palette.png)
  python tools/bake_flat_gltf.py --batch in_dir out_root    -> one folder per model (catalog layout)
"""
import json
import math
import struct
import sys
from pathlib import Path


def read_glb(path: Path):
    """Return (gltf_json_dict, bin_bytes)."""
    data = path.read_bytes()
    if data[:4] != b"glTF":
        raise ValueError(f"{path}: not a GLB")
    _, version, _ = struct.unpack_from("<III", data, 0)
    if version != 2:
        raise ValueError(f"{path}: GLB version {version}")
    off = 12
    js, binblob = None, b""
    while off < len(data):
        clen, ctype = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + clen]
        if ctype == 0x4E4F534A:      # 'JSON'
            js = json.loads(chunk.decode("utf-8"))
        elif ctype == 0x004E4942:    # 'BIN'
            binblob = bytes(chunk)
        off += 8 + clen
    if js is None:
        raise ValueError(f"{path}: GLB missing JSON chunk")
    return js, binblob


def lin_to_srgb(c: float) -> int:
    c = max(0.0, min(1.0, c))
    s = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
    return max(0, min(255, round(s * 255.0)))


def bake(src: Path, out_dir: Path, name: str) -> dict:
    """Bake one model. Returns a small stats dict."""
    from PIL import Image

    if src.suffix.lower() == ".glb":
        g, binblob = read_glb(src)
    else:
        g = json.loads(src.read_text(encoding="utf-8"))
        bufs = g.get("buffers", [])
        if len(bufs) != 1 or "uri" not in bufs[0] or bufs[0]["uri"].startswith("data:"):
            raise ValueError(f"{src}: only single external-bin .gltf supported")
        binblob = (src.parent / bufs[0]["uri"]).read_bytes()

    if g.get("extensionsRequired"):
        raise ValueError(f"{src}: requires extensions {g['extensionsRequired']}")

    materials = g.get("materials", [])
    accessors = g.setdefault("accessors", [])
    views = g.setdefault("bufferViews", [])

    # Flat materials (no baseColorTexture) -> palette blocks.
    flat_idx = {}       # material index -> palette block
    colors = []
    for i, m in enumerate(materials):
        pbr = m.get("pbrMetallicRoughness", {}) or {}
        if "baseColorTexture" in pbr:
            continue
        flat_idx[i] = len(colors)
        colors.append(pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0]))
    if not materials:
        flat_idx[None] = 0
        colors = [[1.0, 1.0, 1.0, 1.0]]
    baked_prims = 0

    if colors:
        block = 64
        img = Image.new("RGB", (block * len(colors), block))
        for bi, col in enumerate(colors):
            px = tuple(lin_to_srgb(col[c]) for c in range(3))
            img.paste(px, (bi * block, 0, (bi + 1) * block, block))

        blob = bytearray(binblob)

        def append_const_uv(u: float, v: float, count: int) -> int:
            """Append `count` copies of (u,v); return new accessor index."""
            while len(blob) % 4:
                blob.append(0)
            offset = len(blob)
            blob.extend(struct.pack("<2f", u, v) * count)
            views.append({"buffer": 0, "byteOffset": offset, "byteLength": count * 8})
            accessors.append({"bufferView": len(views) - 1, "componentType": 5126,
                              "count": count, "type": "VEC2",
                              "min": [u, v], "max": [u, v]})
            return len(accessors) - 1

        # One shared UV accessor per (material block, vertex count).
        uv_cache = {}
        for mesh in g.get("meshes", []):
            for prim in mesh.get("primitives", []):
                mat = prim.get("material")
                key = mat if mat in flat_idx else (None if None in flat_idx else None)
                if key not in flat_idx:
                    continue                     # textured material: leave untouched
                bi = flat_idx[key]
                pos = prim.get("attributes", {}).get("POSITION")
                if pos is None:
                    continue
                count = accessors[pos]["count"]
                u = (bi * block + block / 2) / (block * len(colors))
                ck = (bi, count)
                if ck not in uv_cache:
                    uv_cache[ck] = append_const_uv(u, 0.5, count)
                prim["attributes"]["TEXCOORD_0"] = uv_cache[ck]
                prim["material"] = len(materials)     # the shared palette material (appended below)
                baked_prims += 1

        materials.append({
            "name": "baked_palette",
            "pbrMetallicRoughness": {
                "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                "baseColorTexture": {"index": len(g.get("textures", []))},
                "metallicFactor": 0.0,
                "roughnessFactor": 0.9,
            },
        })
        g["materials"] = materials
        g.setdefault("samplers", []).append(
            {"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071})
        g.setdefault("images", []).append({"uri": f"{name}_palette.png"})
        g.setdefault("textures", []).append(
            {"sampler": len(g["samplers"]) - 1, "source": len(g["images"]) - 1})
        binblob = bytes(blob)
    else:
        img = None

    g["buffers"] = [{"uri": f"{name}.bin", "byteLength": len(binblob)}]

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / f"{name}.bin").write_bytes(binblob)
    if img is not None:
        img.save(out_dir / f"{name}_palette.png")
    (out_dir / f"{name}.gltf").write_text(json.dumps(g, separators=(",", ":")), encoding="utf-8")
    return {"materials": len(colors), "baked_prims": baked_prims}


def main():
    args = [a for a in sys.argv[1:] if a != "--batch"]
    batch = "--batch" in sys.argv
    if batch:
        in_dir, out_root = Path(args[0]), Path(args[1])
        ok = fail = 0
        for f in sorted(in_dir.glob("*.glb")):
            name = f.stem
            try:
                st = bake(f, out_root / name, name)
                ok += 1
                if ok <= 5 or ok % 50 == 0:
                    print(f"  [{ok}] {name}: {st['materials']} colors, {st['baked_prims']} prims")
            except Exception as e:
                fail += 1
                print(f"  FAIL {name}: {e}")
        print(f"baked {ok} models, {fail} failures -> {out_root}")
        sys.exit(1 if fail else 0)
    else:
        src = Path(args[0])
        out = Path(args[1])
        st = bake(src, out, out.name)
        print(f"baked {src.name} -> {out} ({st['materials']} colors, {st['baked_prims']} prims)")


if __name__ == "__main__":
    main()
