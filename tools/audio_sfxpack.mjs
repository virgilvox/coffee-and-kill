// Splits tools/audio_sfx.mjs's sfx.bin into one mono stream (sfx_all.f32, for
// audio_qoa at scale .5: a few SFX peak above 1) and the index the cart reads
// (assets/music/sfx_index.bin: "CKSI", u32 count, then per variant u8 name
// length, name, f32 arg, u32 start frame, u32 frames).
//   node tools/audio_sfxpack.mjs build/music-raw/sfx.bin build/music-raw/sfx_all.f32 assets/music/sfx_index.bin
import { readFileSync, writeFileSync } from 'node:fs';
const [inp, pcmOut, idxOut] = process.argv.slice(2);
const b = readFileSync(inp);
let o = 12; const n = b.readUInt32LE(8);
const pcm = [], idx = [Buffer.from('CKSI'), Buffer.alloc(4)]; idx[1].writeUInt32LE(n);
let at = 0;
for (let i = 0; i < n; i++) {
  const l = b[o], name = b.slice(o + 1, o + 1 + l).toString(), arg = b.readFloatLE(o + 1 + l), fr = b.readUInt32LE(o + 5 + l);
  o += 9 + l;
  pcm.push(b.slice(o, o + fr * 4)); o += fr * 4;
  const h = Buffer.alloc(1 + l + 12); h[0] = l; h.write(name, 1); h.writeFloatLE(arg, 1 + l); h.writeUInt32LE(at, 5 + l); h.writeUInt32LE(fr, 9 + l);
  idx.push(h); at += fr;
}
writeFileSync(pcmOut, Buffer.concat(pcm)); writeFileSync(idxOut, Buffer.concat(idx));
console.log(n, 'variants', at, 'frames');
