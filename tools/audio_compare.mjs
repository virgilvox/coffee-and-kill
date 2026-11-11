// Compares the page's kernel messages (tools/audio_capture.mjs) with the bake
// harness's (LOG_LEVEL mode of tools/audio_bake.mjs): the setup messages one
// for one, then the note events relative to each one's first note.
//   node tools/audio_compare.mjs page.json node.json
import { readFileSync } from 'node:fs';
const [a, b] = process.argv.slice(2).map((f) => JSON.parse(readFileSync(f)));
const setup = (d) => d.msgs.filter((x) => x.m.type !== 'events').map((x) => JSON.stringify(x.m));
const sa = setup(a), sb = setup(b);
let same = 0; for (let i = 0; i < Math.min(sa.length, sb.length); i++) if (sa[i] === sb[i]) same++; else if (same === i) console.log('setup differs at', i, '\n page', sa[i].slice(0, 200), '\n node', sb[i].slice(0, 200));
console.log('setup messages: page', sa.length, 'node', sb.length, 'identical', same);
const notes = (d) => {
  const on = new Map(), out = [];
  for (const x of d.msgs) if (x.m.type === 'events') for (const e of x.m.events) {
    if (e.kind === 0) on.set(e.target + ':' + e.a, e);
    else if (e.kind === 1) { const o = on.get(e.target + ':' + e.a); if (o) out.push({ t: o.time, ch: o.target, f: o.b, v: o.c, dur: e.time - o.time }); }
  }
  out.sort((p, q) => p.t - q.t || p.ch - q.ch || p.f - q.f);
  const t0 = out.length ? out[0].t : 0;
  return out.map((n) => ({ ...n, t: n.t - t0 }));
};
const na = notes(a), nb = notes(b);
const horizon = Math.min(na.at(-1).t, nb.at(-1).t) - 1;
const A = na.filter((n) => n.t < horizon), B = nb.filter((n) => n.t < horizon);
let match = 0, dt = 0, ddur = 0, dv = 0;
const key = (n) => `${n.ch}:${n.f.toFixed(3)}:${Math.round(n.t * 1000)}`;
const bset = new Map(B.map((n) => [key(n), n]));
for (const n of A) { const m = bset.get(key(n)); if (m) { match++; dt = Math.max(dt, Math.abs(m.t - n.t)); ddur = Math.max(ddur, Math.abs(m.dur - n.dur)); dv = Math.max(dv, Math.abs(m.v - n.v)); } }
console.log(`notes within ${horizon.toFixed(1)} s: page ${A.length}, node ${B.length}, matched ${match}; max |dt| ${dt.toExponential(2)} s, |ddur| ${ddur.toExponential(2)} s, |dvel| ${dv}`);
const miss = A.filter((n) => !bset.has(key(n))).slice(0, 5);
if (miss.length) console.log('page notes the harness lacks:', miss);
