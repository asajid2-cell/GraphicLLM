#!/usr/bin/env python3
"""
procgen.py -- GENERATE simple nature models that exist in no corpus at all.

Ladder step 5 of the generative pipeline (after catalog match and Sketchfab fetch):
rocks/boulders/stones/monoliths are synthesized as seeded noise-displaced icospheres
with a flattened base, faceted shading and a flat PBR material, written directly in
the engine loader's dialect (.gltf + external .bin, single material, POSITION/NORMAL
indices -- no textures, no extensions). Deterministic per query.

CLI: python tools/procgen.py "jagged boulder"
API: procgen.ensure("jagged boulder") -> "gen_jagged_boulder" | None
"""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
GEN_DIR = ROOT / "assets" / "models" / "fetched"

GEN_KEYWORDS = ("rock", "boulder", "stone", "pebble", "monolith", "outcrop", "crag",
                "mound", "dune", "hill")


def _icosphere(subdiv=3):
    t = (1.0 + 5 ** 0.5) / 2.0
    verts = np.array([
        [-1, t, 0], [1, t, 0], [-1, -t, 0], [1, -t, 0],
        [0, -1, t], [0, 1, t], [0, -1, -t], [0, 1, -t],
        [t, 0, -1], [t, 0, 1], [-t, 0, -1], [-t, 0, 1],
    ], dtype=np.float64)
    verts /= np.linalg.norm(verts, axis=1, keepdims=True)
    faces = [
        (0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
        (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
        (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
        (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1),
    ]
    verts = list(verts)
    cache = {}

    def mid(a, b):
        key = (min(a, b), max(a, b))
        if key in cache:
            return cache[key]
        m = (np.asarray(verts[a]) + np.asarray(verts[b])) / 2.0
        m /= np.linalg.norm(m)
        verts.append(m)
        cache[key] = len(verts) - 1
        return cache[key]

    for _ in range(subdiv):
        nf = []
        for (a, b, c) in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)]
        faces = nf
    return np.asarray(verts), np.asarray(faces, dtype=np.int64)


def _fbm(points, rng, octaves=4):
    """Cheap seeded fBm on the sphere: sums of randomly-oriented sine plaids."""
    out = np.zeros(len(points))
    amp = 1.0
    for k in range(octaves):
        d = rng.normal(size=3)
        d /= np.linalg.norm(d)
        freq = 1.4 * (1.9 ** k)
        phase = rng.uniform(0, 2 * np.pi)
        out += amp * np.sin(points @ d * freq + phase)
        d2 = rng.normal(size=3)
        d2 /= np.linalg.norm(d2)
        out += 0.6 * amp * np.sin(points @ d2 * freq * 1.31 + rng.uniform(0, 2 * np.pi))
        amp *= 0.55
    return out / 2.4


def make_rock(seed, jagged=0.5):
    rng = np.random.RandomState(seed)
    v, f = _icosphere(3)
    scale = rng.uniform(0.65, 1.35, size=3)          # anisotropic silhouette
    disp = _fbm(v, rng) * (0.18 + 0.30 * jagged)
    v = v * (1.0 + disp[:, None]) * scale[None, :]
    v[:, 1] = np.maximum(v[:, 1], -0.35 * scale[1])   # flatten the base so it sits
    # faceted shading: split every face so each carries its plane normal
    fv = v[f.reshape(-1)].reshape(-1, 3, 3)
    n = np.cross(fv[:, 1] - fv[:, 0], fv[:, 2] - fv[:, 0])
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    positions = fv.reshape(-1, 3).astype(np.float32)
    normals = np.repeat(n, 3, axis=0).astype(np.float32)
    indices = np.arange(len(positions), dtype=np.uint32)
    # ground the base at y=0 (the engine ground-snaps by bounds anyway)
    positions[:, 1] -= positions[:, 1].min()
    grey = rng.uniform(0.22, 0.38)
    warm = rng.uniform(-0.04, 0.05)
    color = [grey + warm, grey, grey - warm * 0.5, 1.0]
    return positions, normals, indices, color


def write_gltf(out_dir, name, positions, normals, indices, base_color):
    """Loader-dialect glTF. The colour ships as a 1-block palette TEXTURE + constant
    UVs (the engine keeps material colour only for TEXTURED meshes; untextured models
    get the command colour instead) -- same trick as the Kenney bake."""
    from PIL import Image
    out_dir.mkdir(parents=True, exist_ok=True)

    def lin_to_srgb(c):
        c = max(0.0, min(1.0, c))
        s = 12.92 * c if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055
        return max(0, min(255, round(s * 255.0)))

    img = Image.new("RGB", (64, 64), tuple(lin_to_srgb(base_color[i]) for i in range(3)))
    img.save(out_dir / f"{name}_palette.png")
    uvs = np.full((len(positions), 2), 0.5, dtype=np.float32)
    pos_b = positions.tobytes()
    nrm_b = normals.tobytes()
    uv_b = uvs.tobytes()
    idx_b = indices.tobytes()
    blob = pos_b + nrm_b + uv_b + idx_b
    (out_dir / f"{name}.bin").write_bytes(blob)
    g = {
        "asset": {"version": "2.0", "generator": "cortex procgen"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0, "mode": 4}]}],
        "materials": [{"name": "rock", "pbrMetallicRoughness": {
            "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
            "baseColorTexture": {"index": 0},
            "metallicFactor": 0.0, "roughnessFactor": 0.93}}],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": f"{name}_palette.png"}],
        "textures": [{"sampler": 0, "source": 0}],
        "buffers": [{"uri": f"{name}.bin", "byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(pos_b)},
            {"buffer": 0, "byteOffset": len(pos_b), "byteLength": len(nrm_b)},
            {"buffer": 0, "byteOffset": len(pos_b) + len(nrm_b), "byteLength": len(uv_b)},
            {"buffer": 0, "byteOffset": len(pos_b) + len(nrm_b) + len(uv_b), "byteLength": len(idx_b)},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": positions.min(axis=0).tolist(), "max": positions.max(axis=0).tolist()},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(indices), "type": "SCALAR"},
        ],
    }
    (out_dir / f"{name}.gltf").write_text(json.dumps(g, separators=(",", ":")), encoding="utf-8")


def ensure(query, verbose=True):
    q = (query or "").lower()
    if not any(k in q for k in GEN_KEYWORDS):
        return None
    slug = "gen_" + re.sub(r"[^a-z0-9]+", "_", q).strip("_")[:40]
    dest = GEN_DIR / slug
    if dest.exists() and any(dest.glob("*.gltf")):
        if verbose:
            print(f"    [procgen] cache hit: {slug}")
        return slug
    seed = int(hashlib.md5(q.encode()).hexdigest()[:8], 16) & 0x7FFFFFFF
    jagged = 0.8 if any(w in q for w in ("jagged", "sharp", "crag")) else 0.45
    positions, normals, indices, color = make_rock(seed, jagged)
    write_gltf(dest, slug, positions, normals, indices, color)
    if verbose:
        print(f"    [procgen] generated {slug}: {len(positions)} verts, seed {seed}")
    try:
        import asset_fetch
        if not asset_fetch.verify_engine_load(slug, verbose=verbose):
            import shutil
            shutil.rmtree(dest, ignore_errors=True)
            return None
    except Exception:
        pass
    return slug


if __name__ == "__main__":
    q = " ".join(sys.argv[1:]) or "boulder"
    got = ensure(q)
    print(got or "FAILED")
    sys.exit(0 if got else 1)
