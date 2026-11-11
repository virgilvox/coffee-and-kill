// Renders every SFX of the page through Chrome's own WebAudio: the page's SA
// code (game.js 1372..1514, the voices built per call) in an
// OfflineAudioContext, with the sfx bus (y8) going straight to the
// destination. The master gain, the compressor and the duck stay live in the
// cart (sfx.c). Noise voices start at a random offset into a random buffer,
// so those are rendered in several variants (Math.random seeded per variant).
//
//   node tools/audio_sfx.mjs <outdir> [--rate 48000]
// Writes <outdir>/sfx.bin: "CKSF", u32 rate, u32 count, then per variant
// u8 name length, name ("hit1" = hit(true)), f32 arg, u32 frames, f32 mono samples.
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { chrome } from './audio_cdp.mjs';

const args = process.argv.slice(2);
const out = args[0];
const RATE = args.includes('--rate') ? +args[args.indexOf('--rate') + 1] : 48000;
const game = readFileSync(new URL('../build/audio-src/game.js', import.meta.url), 'utf8').split('\n');
const code = game.slice(1372 - 1, 1514).join('\n');

const NOISE = 6;   // variants of each voice that draws noise
const list = [
  ['whoosh', [1, 0.82, 1.5, 1.1], NOISE], ['hit', [0, 1], NOISE], ['clang', [0], NOISE], ['block', [0], NOISE],
  ['kick', [0], NOISE], ['shock', [0], NOISE], ['ko', [0], NOISE], ['bell', [0], 1], ['ui', [0], 1], ['uiOk', [0], 1],
  ['step', [0], NOISE], ['dodge', [0], NOISE], ['special', [0], 1], ['fight', [0], NOISE], ['win', [0], 1],
];

const c = await chrome({ port: 9334 });
await c.evaluate(`window.__sfxcode = ${JSON.stringify(code)}; true`);
const parts = [];
let count = 0;
for (const [name, argsList, variants] of list) {
  for (const arg of argsList) for (let v = 0; v < variants; v++) {
    const b64 = await c.evaluate(`(async () => {
      let seed = ${v * 7919 + 17};
      const rnd = () => { seed |= 0; seed = seed + 1831565813 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
      const ctx = new OfflineAudioContext(1, ${RATE} * 2, ${RATE});
      const run = new Function('ctx', 'rnd', 'arg', 'name', window.__sfxcode + \`
        const R = Math.random; Math.random = rnd;
        try {
          UP = ctx; pD = ctx.createGain(); y8 = ctx.createGain(); y8.gain.value = 1; y8.connect(ctx.destination);
          HD = ctx.createGain(); HD.gain.value = 0.36;
          let P = ctx.sampleRate * 1.5; KB = ctx.createBuffer(1, P, ctx.sampleRate);
          let e = KB.getChannelData(0); for (let t = 0; t < P; t++) e[t] = Math.random() * 2 - 1;
          if (name === 'hit') QP.hit(!!arg); else if (name === 'whoosh') QP.whoosh(arg); else QP[name]();
        } finally { Math.random = R; }\`);
      run(ctx, rnd, ${arg}, ${JSON.stringify(name)});
      const buf = await ctx.startRendering();
      const d = buf.getChannelData(0);
      let n = d.length; while (n > 0 && Math.abs(d[n - 1]) < 1e-5) n--;
      const u8 = new Uint8Array(d.buffer, 0, n * 4);
      let s = ''; for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
      return btoa(s);
    })()`);
    const pcm = Buffer.from(b64, 'base64');
    const label = name === 'hit' ? 'hit' + arg : name;
    const head = Buffer.alloc(1 + label.length + 8);
    head.writeUInt8(label.length, 0); head.write(label, 1); head.writeFloatLE(arg, 1 + label.length); head.writeUInt32LE(pcm.length / 4, 5 + label.length);
    parts.push(head, pcm); count++;
    console.log(label, arg, v, (pcm.length / 4 / RATE).toFixed(3) + 's');
  }
}
c.close();
const hdr = Buffer.alloc(12); hdr.write('CKSF', 0); hdr.writeUInt32LE(RATE, 4); hdr.writeUInt32LE(count, 8);
writeFileSync(join(out, 'sfx.bin'), Buffer.concat([hdr, ...parts]));
console.log('wrote', count, 'variants');
