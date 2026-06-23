// Fetch Khronos glTF-Sample-Assets furniture (glTF text variant: .gltf + .bin + textures).
import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";
const BASE = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models";
const OUT = "assets/models/khronos_furniture";
async function get(url){ const r = await fetch(url); if(!r.ok) throw new Error(r.status+" "+url); return r; }
for (const name of process.argv.slice(2)) {
  const dir = join(OUT, name); mkdirSync(dir, { recursive: true });
  const gltfUrl = `${BASE}/${name}/glTF/${name}.gltf`;
  const gltfTxt = await (await get(gltfUrl)).text();
  writeFileSync(join(dir, `${name}.gltf`), gltfTxt);
  const j = JSON.parse(gltfTxt);
  const uris = new Set();
  for (const b of (j.buffers||[])) if (b.uri && !b.uri.startsWith("data:")) uris.add(b.uri);
  for (const im of (j.images||[])) if (im.uri && !im.uri.startsWith("data:")) uris.add(im.uri);
  for (const u of uris) {
    const dec = decodeURIComponent(u);
    const buf = Buffer.from(await (await get(`${BASE}/${name}/glTF/${u}`)).arrayBuffer());
    const outp = join(dir, dec); mkdirSync(join(outp, ".."), { recursive: true });
    writeFileSync(outp, buf);
  }
  console.log(`OK ${name}: gltf + ${uris.size} files`);
}
