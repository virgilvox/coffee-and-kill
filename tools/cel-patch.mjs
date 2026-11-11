// From KEYHOLDER (its tools/cel-patch.mjs): the cel look of its bodies. three.c has no toon material, but its programs are three.js's captured GLSL as plain text. This turns
// the character states' lambert into two cel bands before the table is emitted: the direct light's
// clamp(dot(n, l), 0, 1) becomes a hard step. Run between two gen-project-table passes (build.sh does).
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
const dir = process.argv[2];
let n = 0;
for (const f of readdirSync(dir)) {
  if (!/^lambert.*skin.*\.gl\.frag$/.test(f)) continue;
  const p = join(dir, f), s = readFileSync(p, 'utf8');
  const o = s.replace(/vec3\( clamp\( (nodeVar\d+), 0\.0, 1\.0 \) \)/g, 'vec3( smoothstep( 0.16, 0.24, $1 ) )');
  if (o !== s) { writeFileSync(p, o); n++; }
}
console.log(`cel patch: ${n} programs`);
