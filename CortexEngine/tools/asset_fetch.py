#!/usr/bin/env python3
"""
asset_fetch.py -- fetch an ARBITRARY model by free-text query into the engine catalog.

Ladder step 4 of the generative pipeline: when a composer query ("beach umbrella",
"flamingo float", "lighthouse") matches nothing in the local corpus, search Sketchfab
(downloadable models only), download the glTF archive, NORMALIZE it to the engine
loader's dialect (.gltf + external .bin + external textures; draco decoded; textures
capped at 1k), land it in assets/models/fetched/<slug>/<slug>.gltf and verify the
engine can actually load + measure it. The catalog directory scan picks it up on the
next launch -- no rebuild.

Auth: SKETCHFAB_TOKEN env var (search is anonymous; download needs the token).
The token is never logged and never written to disk. Attribution (author/license)
is recorded in CREDITS.txt beside each fetched model.

CLI: python tools/asset_fetch.py "beach umbrella"
API: asset_fetch.ensure("beach umbrella") -> "fetched_beach_umbrella" | None
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FETCH_DIR = ROOT / "assets" / "models" / "fetched"
EXE = ROOT / "build" / "bin" / "CortexEngine.exe"

MAX_FACES = 250_000
MAX_ARCHIVE_MB = 90
SEARCH_COUNT = 24


def _slug(query):
    s = re.sub(r"[^a-z0-9]+", "_", query.lower()).strip("_")
    return ("fetched_" + s)[:48]


def _get_json(url, token=None, timeout=30):
    req = urllib.request.Request(url)
    if token:
        req.add_header("Authorization", f"Token {token}")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode())


def search(query, verbose=True):
    """Best downloadable Sketchfab candidates for the query, most-liked first."""
    url = ("https://api.sketchfab.com/v3/search?type=models&downloadable=true"
           f"&count={SEARCH_COUNT}&q=" + urllib.parse.quote(query))
    data = _get_json(url)
    out = []
    for r in data.get("results", []):
        if not r.get("isDownloadable") or r.get("isAgeRestricted"):
            continue
        faces = r.get("faceCount") or 0
        gltf_arch = (r.get("archives") or {}).get("gltf") or {}
        size_mb = (gltf_arch.get("size") or 0) / 1e6
        if faces > MAX_FACES or size_mb > MAX_ARCHIVE_MB or size_mb <= 0:
            continue
        out.append(r)
    out.sort(key=lambda r: -(r.get("likeCount") or 0))
    if verbose:
        for r in out[:3]:
            print(f"    [search] {r['name'][:40]!r} faces={r['faceCount']} "
                  f"likes={r.get('likeCount')} license={(r.get('license') or {}).get('label')}")
    return out


def download_archive(uid, token, dest_zip, verbose=True):
    dl = _get_json(f"https://api.sketchfab.com/v3/models/{uid}/download", token=token)
    url = ((dl.get("gltf") or {}).get("url")) or ((dl.get("glb") or {}).get("url"))
    if not url:
        return False
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=300) as resp, open(dest_zip, "wb") as f:
        shutil.copyfileobj(resp, f)
    return True


def _npx_gltf_transform(args, timeout=300):
    cmd = ["npx", "--yes", "@gltf-transform/cli"] + args
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, shell=(os.name == "nt"))


def normalize(src_model, out_dir, slug, verbose=True):
    """glb/gltf (possibly draco, embedded buffers) -> loader-dialect gltf+bin+textures."""
    out_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        mid = Path(td) / "resized.glb"
        # resize textures first (works on any input; writes a self-contained glb)
        r = _npx_gltf_transform(["resize", str(src_model), str(mid), "--width", "1024", "--height", "1024"])
        if r.returncode != 0:
            mid = src_model            # resize is best-effort
        final = out_dir / f"{slug}.gltf"
        r = _npx_gltf_transform(["copy", str(mid), str(final)])
        if r.returncode != 0:
            if verbose:
                print(f"    [normalize] gltf-transform copy failed: {r.stderr[-200:]}")
            return None
    # loader-compat sanity: external buffers only, no required extensions
    try:
        g = json.loads(final.read_text(encoding="utf-8"))
        if g.get("extensionsRequired"):
            if verbose:
                print(f"    [normalize] requires extensions {g['extensionsRequired']} -> reject")
            return None
        for b in g.get("buffers", []):
            if "uri" not in b or b["uri"].startswith("data:"):
                if verbose:
                    print("    [normalize] embedded buffer survived -> reject")
                return None
    except Exception as e:
        if verbose:
            print(f"    [normalize] unreadable output: {e}")
        return None
    return final


def verify_engine_load(slug, verbose=True):
    """The engine itself must load + measure the fetched mesh (the same LoadGLTFMesh
    path Place() uses). Anything it can't load is discarded."""
    if not EXE.exists():
        return True                    # can't verify without a build; accept optimistically
    r = subprocess.run([str(EXE), "--dump-catalog", "--measure", "--no-launcher"],
                       capture_output=True, text=True, cwd=str(EXE.parent), timeout=300)
    s = r.stdout
    i = s.find("{")
    if i < 0:
        return False
    d = json.loads(s[i:])
    for a in d.get("assets", []):
        if a["id"].lower() == slug.lower():
            ok = a.get("native_size") is not None
            if verbose:
                print(f"    [verify] engine load {'OK' if ok else 'FAILED'} "
                      f"(native_size={a.get('native_size')})")
            return ok
    if verbose:
        print("    [verify] not visible in catalog scan")
    return False


def ensure(query, verbose=True):
    """Return a catalog asset id for the query, fetching it if necessary. None = give up
    (no token / offline / nothing suitable / normalize or load failed)."""
    slug = _slug(query)
    dest = FETCH_DIR / slug
    if dest.exists() and any(dest.glob("*.gltf")):
        if verbose:
            print(f"    [fetch] cache hit: {slug}")
        return slug
    token = os.environ.get("SKETCHFAB_TOKEN")
    if not token:
        if verbose:
            print("    [fetch] no SKETCHFAB_TOKEN -> skip")
        return None
    try:
        candidates = search(query, verbose=verbose)
    except Exception as e:
        if verbose:
            print(f"    [fetch] search failed: {e}")
        return None
    for cand in candidates[:3]:
        try:
            with tempfile.TemporaryDirectory() as td:
                zip_path = Path(td) / "model.zip"
                if verbose:
                    print(f"    [fetch] downloading {cand['name'][:40]!r} ({cand['uid']})")
                if not download_archive(cand["uid"], token, zip_path, verbose=verbose):
                    continue
                extract = Path(td) / "x"
                with zipfile.ZipFile(zip_path) as zf:
                    zf.extractall(extract)
                model = next(iter(sorted(extract.rglob("*.gltf"))), None) or \
                        next(iter(sorted(extract.rglob("*.glb"))), None)
                if model is None:
                    continue
                final = normalize(model, dest, slug, verbose=verbose)
            if final is None:
                shutil.rmtree(dest, ignore_errors=True)
                continue
            user = (cand.get("user") or {}).get("displayName", "?")
            lic = (cand.get("license") or {}).get("label", "?")
            (dest / "CREDITS.txt").write_text(
                f"{cand['name']}\nby {user}\nlicense: {lic}\n{cand.get('viewerUrl','')}\n"
                f"fetched via Sketchfab API for query: {query}\n", encoding="utf-8")
            if not verify_engine_load(slug, verbose=verbose):
                shutil.rmtree(dest, ignore_errors=True)
                continue
            if verbose:
                print(f"    [fetch] OK: {slug} <- {cand['name'][:40]!r} ({lic})")
            return slug
        except Exception as e:
            if verbose:
                print(f"    [fetch] candidate failed: {e}")
            shutil.rmtree(dest, ignore_errors=True)
            continue
    return None


if __name__ == "__main__":
    q = " ".join(sys.argv[1:])
    if not q:
        print("usage: asset_fetch.py <query>")
        sys.exit(2)
    got = ensure(q)
    print(got or "FAILED")
    sys.exit(0 if got else 1)
