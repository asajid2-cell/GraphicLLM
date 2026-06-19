// Fetch CC0 high-poly glTF models from Poly Haven into the engine's asset catalog.
//
//   node tools/fetch_polyhaven.mjs Sofa_01 ArmChair_01 CoffeeTable_01 ...
//
// For each asset id it pulls the 1k glTF (gltf + .bin + diff/normal/ARM textures)
// from the Poly Haven file API and lays it out as
//   assets/models/naturalistic_showcase/<id>/<id>_1k.gltf
//   assets/models/naturalistic_showcase/<id>/<id>.bin
//   assets/models/naturalistic_showcase/<id>/textures/*.jpg
// which is exactly the structure AssetCatalog scans (so the id is usable in recipes
// via Place(out, cat, c, "<id>", ...)). The CMake asset glob is CONFIGURE_DEPENDS,
// so a normal build copies the new files into build/bin. Use the asset's real
// texture URLs from the API (texture names are NOT always "<id>_<type>_1k.jpg").
import { mkdirSync, createWriteStream, existsSync, statSync } from "node:fs";
import { dirname, join } from "node:path";

const ROOT = join(import.meta.dirname, "..", "assets", "models", "naturalistic_showcase");

async function getJson(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> HTTP ${r.status}`);
  return r.json();
}

async function download(url, dest) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url} -> HTTP ${r.status}`);
  const buf = Buffer.from(await r.arrayBuffer());
  if (buf.length < 200) throw new Error(`${url} -> suspiciously small (${buf.length}B), likely an error payload`);
  mkdirSync(dirname(dest), { recursive: true });
  await new Promise((res, rej) => {
    const w = createWriteStream(dest);
    w.on("error", rej); w.on("finish", res); w.end(buf);
  });
  return buf.length;
}

// Walk the {url, include:{...}} tree the files API returns for a gltf entry.
function collectFiles(node, acc = []) {
  if (node && typeof node === "object") {
    if (typeof node.url === "string") acc.push(node.url);
    for (const k of Object.keys(node)) if (k !== "url") collectFiles(node[k], acc);
  }
  return acc;
}

async function fetchAsset(id) {
  const dir = join(ROOT, id);
  const files = await getJson(`https://api.polyhaven.com/files/${id}`);
  const g = files?.gltf?.["1k"]?.gltf;
  if (!g?.url) throw new Error(`${id}: no gltf/1k entry (is it a model with a glTF export?)`);

  // main gltf
  let total = await download(g.url, join(dir, `${id}_1k.gltf`));
  // included bin + textures, preserving their relative path under the asset dir
  const inc = g.include || {};
  for (const [rel, info] of Object.entries(inc)) {
    if (!info?.url) continue;
    total += await download(info.url, join(dir, rel));
  }
  return { dir, bytes: total };
}

const ids = process.argv.slice(2);
if (!ids.length) { console.error("usage: node tools/fetch_polyhaven.mjs <AssetId> [AssetId...]"); process.exit(2); }

let ok = 0;
for (const id of ids) {
  try {
    const { dir, bytes } = await fetchAsset(id);
    console.log(`OK   ${id}  (${(bytes / 1024).toFixed(0)} KB) -> ${dir}`);
    ok++;
  } catch (e) {
    console.error(`FAIL ${id}: ${e.message}`);
  }
}
console.log(`\n${ok}/${ids.length} assets fetched. Rebuild to copy into build/bin, then use the id in a recipe.`);
