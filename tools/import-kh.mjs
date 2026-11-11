// Brings KEYHOLDER's assets in: the bodies the roster names (src/roster.c, KH("<letter>", ...)),
// the lab, the five Battle Zone arenas and the street signs, plus the generated tables its modules
// read (src/kh/gen). Run it after adding a fighter with a new KEYHOLDER body, or when KEYHOLDER
// rebakes its world. tools/rig-vroid.mjs makes the bodies' clips.
//
//   node tools/import-kh.mjs [path/to/keyholder]      (default ../keyholder)
import { readFileSync, writeFileSync, mkdirSync, copyFileSync, readdirSync, rmSync, existsSync, statSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const KH = resolve(process.argv[2] || join(ROOT, '..', 'keyholder'));
const OUT = join(ROOT, 'assets/kh');

const roster = readFileSync(join(ROOT, 'src/roster.c'), 'utf8');
const letters = [...new Set([...roster.matchAll(/KH\("([A-Z0-9]+)"/g)].map((m) => m[1]))];
if (!letters.length) throw new Error('no KEYHOLDER bodies in src/roster.c');

if (existsSync(OUT)) rmSync(OUT, { recursive: true });
mkdirSync(OUT, { recursive: true });
let bytes = 0;
const copy = (from, to) => { copyFileSync(from, join(OUT, to)); bytes += statSync(from).size; };
for (const l of letters) {
  const id = 'char_' + l.toLowerCase();
  copy(join(KH, 'assets/models', id + '.glb'), 'kh_' + id + '.glb');
  copy(join(KH, 'assets/models', id + '.spr'), 'kh_' + id + '.spr');
}
copy(join(KH, 'assets/models/lab.glb'), 'lab.glb');
for (const f of readdirSync(join(KH, 'assets/models'))) if (/^arena_.*\.glb$/.test(f)) copy(join(KH, 'assets/models', f), f);
copy(join(KH, 'assets/signs/city_signs.png'), 'city_signs.png');

mkdirSync(join(ROOT, 'src/kh/gen'), { recursive: true });
for (const g of ['cast_meta.h', 'arena_data.h', 'city_data.h', 'lab_areas.h', 'sign_atlas.h'])
  writeFileSync(join(ROOT, 'src/kh/gen', g), readFileSync(join(KH, 'src/gen', g)));

console.log(`import-kh: bodies ${letters.join(' ')}; lab, arenas and signs; ${(bytes / 1048576).toFixed(1)} MB in assets/kh`);
