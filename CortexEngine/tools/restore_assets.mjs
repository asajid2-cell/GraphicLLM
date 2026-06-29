// Restore the large binary assets that are intentionally NOT tracked in git
// (see tools/assets_manifest.json). Downloads anything missing so a fresh
// clone becomes runnable without bloating the repo.
//
//   cd CortexEngine
//   node tools/restore_assets.mjs            # restore everything missing
//   node tools/restore_assets.mjs --only sketchfab_furniture hdris_polyhaven
//   node tools/restore_assets.mjs --list     # show status, download nothing
//
// Sketchfab assets need a token:  SKETCHFAB_TOKEN=xxxx node tools/restore_assets.mjs
// (never commit the token). Khronos / Poly Haven are public, no auth.
import { mkdirSync, writeFileSync, existsSync, readdirSync, rmSync, readFileSync } from "node:fs";
import { join, dirname, basename } from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = join(HERE, "..");                       // CortexEngine/
const manifest = JSON.parse(readFileSync(join(HERE, "assets_manifest.json"), "utf8"));

const argv = process.argv.slice(2);
const LIST_ONLY = argv.includes("--list");
const onlyIdx = argv.indexOf("--only");
const ONLY = onlyIdx >= 0 ? argv.slice(onlyIdx + 1) : null;

const abs = (p) => join(ROOT, p);
async function get(url) { const r = await fetch(url); if (!r.ok) throw new Error(`HTTP ${r.status} ${url}`); return r; }
async function dl(url, dest) {
  // get() already throws on non-2xx; only reject a truly empty body here
  // (valid assets can be tiny, e.g. a 126B solid-colour normal map).
  const buf = Buffer.from(await (await get(url)).arrayBuffer());
  if (buf.length === 0) throw new Error(`${url} -> empty response`);
  mkdirSync(dirname(dest), { recursive: true });
  writeFileSync(dest, buf);
  return buf.length;
}
const hasGltf = (dir) => { if (!existsSync(dir)) return false; try { return readdirSync(dir).some(f => f.endsWith(".gltf")); } catch { return false; } };

// ---- per-type resolvers ----------------------------------------------------
const KHRONOS = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models";
async function khronos(name, target) {
  const dir = abs(join(target, name));
  if (hasGltf(dir)) return "present";
  const gltfTxt = await (await get(`${KHRONOS}/${name}/glTF/${name}.gltf`)).text();
  mkdirSync(dir, { recursive: true });
  writeFileSync(join(dir, `${name}.gltf`), gltfTxt);
  const j = JSON.parse(gltfTxt);
  const uris = new Set();
  for (const b of (j.buffers || [])) if (b.uri && !b.uri.startsWith("data:")) uris.add(b.uri);
  for (const im of (j.images || [])) if (im.uri && !im.uri.startsWith("data:")) uris.add(im.uri);
  for (const u of uris) await dl(`${KHRONOS}/${name}/glTF/${u}`, join(dir, decodeURIComponent(u)));
  return `downloaded (+${uris.size} files)`;
}

async function sketchfab(uid, name, target, token) {
  const dir = abs(join(target, name));
  if (hasGltf(dir)) return "present";
  if (!token) throw new Error("SKETCHFAB_TOKEN not set");
  const dj = await (await fetch(`https://api.sketchfab.com/v3/models/${uid}/download`, { headers: { Authorization: "Token " + token } })).json();
  if (!dj.gltf || !dj.gltf.url) throw new Error("no gltf download url");
  mkdirSync(dir, { recursive: true });
  const zip = join(dir, "_dl.zip");
  writeFileSync(zip, Buffer.from(await (await fetch(dj.gltf.url)).arrayBuffer()));
  execFileSync("powershell", ["-NoProfile", "-Command", `Expand-Archive -LiteralPath '${zip}' -DestinationPath '${dir}' -Force`], { stdio: "ignore" });
  rmSync(zip, { force: true });
  return "downloaded";
}

const PH = "https://api.polyhaven.com/files";
function collectUrls(node, acc = []) {
  if (node && typeof node === "object") {
    if (typeof node.url === "string") acc.push(node.url);
    for (const k of Object.keys(node)) if (k !== "url") collectUrls(node[k], acc);
  }
  return acc;
}
async function polyhavenModel(id, target, res) {
  const dir = abs(join(target, id));
  if (hasGltf(dir)) return "present";
  const files = await (await get(`${PH}/${id}`)).json();
  const g = files?.gltf?.[res]?.gltf;
  if (!g?.url) throw new Error(`${id}: no gltf/${res} entry`);
  await dl(g.url, join(dir, `${id}_${res}.gltf`));
  let n = 0;
  for (const [rel, info] of Object.entries(g.include || {})) if (info?.url) { await dl(info.url, join(dir, rel)); n++; }
  return `downloaded (+${n} files)`;
}
async function polyhavenTexture(item, target, res) {
  const { id, files } = item;
  const dir = abs(target);
  if (files.every(f => existsSync(join(dir, f)))) return "present";
  const api = await (await get(`${PH}/${id}`)).json();
  const urls = [];
  for (const map of Object.keys(api)) { const entry = api[map]?.[res]; if (entry) collectUrls(entry, urls); }
  const byName = new Map(urls.map(u => [basename(new URL(u).pathname), u]));
  let n = 0;
  for (const f of files) {
    if (existsSync(join(dir, f))) continue;
    const u = byName.get(f);
    if (!u) throw new Error(`${id}: no API url for ${f}`);
    await dl(u, join(dir, f)); n++;
  }
  return `downloaded (${n} maps)`;
}
async function polyhavenHdri(id, target, res, fmt) {
  const files = await (await get(`${PH}/${id}`)).json();
  const entry = files?.hdri?.[res]?.[fmt];
  if (!entry?.url) throw new Error(`${id}: no hdri/${res}/${fmt}`);
  const dest = join(abs(target), basename(new URL(entry.url).pathname));
  if (existsSync(dest)) return "present";
  await dl(entry.url, dest);
  return "downloaded";
}

// ---- driver ----------------------------------------------------------------
const token = process.env.SKETCHFAB_TOKEN;
let restored = 0, present = 0, failed = 0, manual = 0, needToken = 0;

for (const g of manifest.groups) {
  if (ONLY && !ONLY.includes(g.id)) continue;
  console.log(`\n[${g.id}]  (${g.type})`);

  if (g.type === "manual") {
    const items = g.items || [basename(g.target)];
    const missing = items.filter(it => !existsSync(abs(join(g.target, it))) && !existsSync(abs(g.target)));
    if (existsSync(abs(g.target)) && readdirSync(abs(g.target)).length) { console.log("  present (manual asset already on disk)"); present++; }
    else { console.log(`  MANUAL: ${g.note}`); manual++; }
    continue;
  }

  for (const item of g.items) {
    const label = (typeof item === "object") ? (item.name || item.id) : item;
    try {
      if (LIST_ONLY) {
        let ok;
        const tdir = abs(g.target);
        if (g.type === "polyhaven_texture") ok = item.files.every(f => existsSync(join(tdir, f)));
        else if (g.type === "polyhaven_hdri") ok = existsSync(tdir) && readdirSync(tdir).some(f => f.startsWith(label));
        else ok = hasGltf(abs(join(g.target, label)));
        console.log(`  ${ok ? "present" : "MISSING"}  ${label}`);
        continue;
      }
      let r;
      if (g.type === "khronos") r = await khronos(label, g.target);
      else if (g.type === "sketchfab") { if (!token) { console.log(`  NEEDS TOKEN  ${label}`); needToken++; continue; } r = await sketchfab(item.uid, item.name, g.target, token); }
      else if (g.type === "polyhaven_model") r = await polyhavenModel(label, g.target, g.res || "1k");
      else if (g.type === "polyhaven_texture") r = await polyhavenTexture(item, g.target, g.res || "4k");
      else if (g.type === "polyhaven_hdri") r = await polyhavenHdri(label, g.target, g.res || "4k", g.format || "exr");
      else throw new Error(`unknown type ${g.type}`);
      console.log(`  ${r === "present" ? "present" : "OK " + r}  ${label}`);
      if (r === "present") present++; else restored++;
    } catch (e) { console.log(`  FAIL  ${label}: ${e.message}`); failed++; }
  }
}

console.log(`\n=== restore summary ===`);
console.log(`  downloaded: ${restored}   already present: ${present}   failed: ${failed}   manual: ${manual}` + (needToken ? `   need SKETCHFAB_TOKEN: ${needToken}` : ""));
if (needToken) console.log(`  -> set SKETCHFAB_TOKEN to fetch the Sketchfab furniture, then re-run.`);
if (failed) process.exitCode = 1;
