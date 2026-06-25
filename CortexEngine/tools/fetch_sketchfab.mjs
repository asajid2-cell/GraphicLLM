// Fetch downloadable Sketchfab glTF by uid. Token via env SKETCHFAB_TOKEN (never commit it).
// Usage: SKETCHFAB_TOKEN=... node tools/fetch_sketchfab.mjs <uid> <localName> [<uid> <localName> ...]
import { mkdirSync, writeFileSync, readdirSync, rmSync } from "node:fs";
import { join } from "node:path";
import { execFileSync } from "node:child_process";
const T = process.env.SKETCHFAB_TOKEN;
if (!T) { console.error("SKETCHFAB_TOKEN env required"); process.exit(2); }
const OUT = "assets/models/sketchfab_furniture";
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i += 2) {
  const uid = args[i], name = args[i+1];
  const dir = join(OUT, name); mkdirSync(dir, { recursive: true });
  const dj = await (await fetch(`https://api.sketchfab.com/v3/models/${uid}/download`, { headers: { Authorization: "Token " + T } })).json();
  if (!dj.gltf || !dj.gltf.url) { console.error(`${name}: no gltf url`); continue; }
  const zip = join(dir, "_dl.zip");
  writeFileSync(zip, Buffer.from(await (await fetch(dj.gltf.url)).arrayBuffer()));
  execFileSync("powershell", ["-NoProfile","-Command",`Expand-Archive -LiteralPath '${zip}' -DestinationPath '${dir}' -Force`], { stdio: "ignore" });
  rmSync(zip, { force: true });
  let gltf = null; const walk = (d) => { for (const e of readdirSync(d, { withFileTypes: true })) { const p = join(d, e.name); if (e.isDirectory()) walk(p); else if (e.name.endsWith(".gltf")) gltf = p; } };
  walk(dir);
  console.log(`OK ${name} (${uid}): ${gltf ? gltf.replace(dir,"").replace(/^[\/]/,"") : "NO .gltf"}`);
}
