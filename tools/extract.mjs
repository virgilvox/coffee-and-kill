// Pulls the original's embedded assets out of coffee-and-kill.html:
// the GLB models (Wr), the baked animation clips (U0) and the three woff2 fonts.
//   node tools/extract.mjs <coffee-and-kill.html> <out assets dir>
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
const [html, out] = process.argv.slice(2);
const t = readFileSync(html, 'utf8');
function literal(start) {
  let i = t.indexOf('{', start), depth = 0, q = null;
  for (let j = i; j < t.length; j++) {
    const c = t[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === '"' || c === "'" || c === '`') q = c;
    else if (c === '{') depth++;
    else if (c === '}' && --depth === 0) return t.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const evalLit = (s) => Function('"use strict";return (' + s + ')')();
const Wr = evalLit(literal(t.indexOf('Wr={')));
mkdirSync(join(out, 'models'), { recursive: true });
let n = 0;
for (const [k, v] of Object.entries(Wr)) { writeFileSync(join(out, 'models', k + '.glb'), Buffer.from(v, 'base64')); n++; }
console.log('models', n);
const U0 = evalLit(literal(t.indexOf('U0={')));
mkdirSync(join(out, 'anim'), { recursive: true });
for (const [rig, r] of Object.entries(U0)) {
  console.log('rig', rig, Object.keys(r), 'bones', r.bones.length, 'clips', Object.keys(r.clips).length);
  // rig_<x>.bin: "CKAN" u32 version, u32 nbones, names (u8 len + bytes), u32 nclips,
  // per clip: u8 len + name, f32 dur, u32 nf, f32 hand[4] (peak,t,a0,a1), f32 foot[4],
  // i16 quats[nf*nbones*4], f32 hips[nf*3]
  const parts = [];
  const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v); parts.push(b); };
  const f32 = (v) => { const b = Buffer.alloc(4); b.writeFloatLE(v); parts.push(b); };
  const str = (s) => { const b = Buffer.from(s, 'utf8'); parts.push(Buffer.from([b.length]), b); };
  parts.push(Buffer.from('CKAN')); u32(1); u32(r.bones.length); for (const b of r.bones) str(b);
  const clips = Object.entries(r.clips); u32(clips.length);
  for (const [name, c] of clips) {
    str(name); f32(c.dur); u32(c.nf);
    for (const w of [c.handWin, c.footWin]) for (const k of ['peak', 't', 'a0', 'a1']) f32(w ? w[k] : 0);
    const q = Buffer.from(c.quats, 'base64'), h = Buffer.from(c.hips, 'base64');
    if (q.length !== c.nf * r.bones.length * 8 || h.length !== c.nf * 12) throw new Error(`size ${rig}/${name} ${q.length} ${h.length}`);
    parts.push(q, h);
  }
  writeFileSync(join(out, 'anim', `rig_${rig}.bin`), Buffer.concat(parts));
}
const fonts = [...t.matchAll(/font-family:"([^"]+)";[^}]*?src:url\(data:font\/woff2;base64,([A-Za-z0-9+/=]+)\)/g)];
mkdirSync(join(out, 'fonts'), { recursive: true });
for (const [, fam, b64] of fonts) { const f = fam.replace(/\s+/g, ''); writeFileSync(join(out, 'fonts', f + '.woff2'), Buffer.from(b64, 'base64')); console.log('font', fam); }
