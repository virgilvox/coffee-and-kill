// Checks sfx.c's port of Chromium's DynamicsCompressorKernel against Chrome:
// the same stereo input (6 s of the baked wl loop at the music bus level, with
// baked SFX laid over it, all through pD's 0.8) through a
// DynamicsCompressorNode(-14 dB, ratio 5) in an OfflineAudioContext, and
// through the C port (built natively), compared sample by sample.
//   node tools/audio_comptest.mjs
import { readFileSync, writeFileSync } from 'node:fs';
import { execSync } from 'node:child_process';
import { chrome } from './audio_cdp.mjs';
const R = 48000, N = R * 6;
const m = readFileSync('build/music-raw/fight2.f32'); const mus = new Float32Array(m.buffer, m.byteOffset, m.length / 4);
const s = readFileSync('build/music-raw/sfx_all.f32'); const sfx = new Float32Array(s.buffer, s.byteOffset, s.length / 4);
const L = new Float32Array(N), Rr = new Float32Array(N);
for (let i = 0; i < N; i++) { L[i] = mus[i * 2] * 0.36; Rr[i] = mus[i * 2 + 1] * 0.36; }
// SFX hits every 0.4 s, taken from the packed stream at varied offsets
for (let k = 0; k < 14; k++) { const at = Math.round((0.2 + k * 0.4) * R), from = (k * 137771) % (sfx.length - R); for (let i = 0; i < R * 0.35 && at + i < N; i++) { L[at + i] += sfx[from + i] * 1.5; Rr[at + i] += sfx[from + i] * 1.5; } }
for (let i = 0; i < N; i++) { L[i] *= 0.8; Rr[i] *= 0.8; }
const inter = new Float32Array(N * 2); for (let i = 0; i < N; i++) { inter[i * 2] = L[i]; inter[i * 2 + 1] = Rr[i]; }
writeFileSync('build/atest/comp_in.f32', Buffer.from(inter.buffer));
// Chrome
const c = await chrome({ port: 9336 });
const b64 = Buffer.from(inter.buffer).toString('base64');
await c.evaluate(`window.__in = ${JSON.stringify(b64)}; true`);
const outB64 = await c.evaluate(`(async () => {
  const bin = atob(window.__in), u8 = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
  const x = new Float32Array(u8.buffer), n = x.length / 2;
  const ctx = new OfflineAudioContext(2, n, ${R});
  const buf = ctx.createBuffer(2, n, ${R}); const l = buf.getChannelData(0), r = buf.getChannelData(1);
  for (let i = 0; i < n; i++) { l[i] = x[i * 2]; r[i] = x[i * 2 + 1]; }
  const src = ctx.createBufferSource(); src.buffer = buf;
  const comp = ctx.createDynamicsCompressor(); comp.threshold.value = -14; comp.ratio.value = 5;
  src.connect(comp); comp.connect(ctx.destination); src.start(0);
  const o = await ctx.startRendering(); const ol = o.getChannelData(0), or = o.getChannelData(1);
  const y = new Float32Array(n * 2); for (let i = 0; i < n; i++) { y[i * 2] = ol[i]; y[i * 2 + 1] = or[i]; }
  const yu = new Uint8Array(y.buffer); let s = ''; for (let i = 0; i < yu.length; i += 0x8000) s += String.fromCharCode.apply(null, yu.subarray(i, i + 0x8000));
  return btoa(s); })()`);
c.close();
const chromeOut = new Float32Array(Buffer.from(outB64, 'base64').buffer.slice(0));
// the C port
writeFileSync('build/atest/comp_main.c', `#include <stdio.h>
#include <stdlib.h>
void sfx_test_comp(float *L, float *R, int frames);
int main(void) { FILE *f = fopen("build/atest/comp_in.f32", "rb"); int n = ${N}; float *x = malloc(n * 8); fread(x, 8, n, f); fclose(f);
  float *L = malloc(n * 4), *R = malloc(n * 4); for (int i = 0; i < n; i++) { L[i] = x[i * 2]; R[i] = x[i * 2 + 1]; }
  sfx_test_comp(L, R, n); for (int i = 0; i < n; i++) { x[i * 2] = L[i]; x[i * 2 + 1] = R[i]; }
  f = fopen("build/atest/comp_c.f32", "wb"); fwrite(x, 8, n, f); fclose(f); return 0; }
`);
writeFileSync('build/atest/comp_stubs.c', `#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
uint8_t *ck_asset(const char *n, size_t *l) { (void)n; *l = 0; return 0; }
float ck_rand(void) { return 0.5f; }
void ck_log(const char *f, ...) { (void)f; }
void music_init(void) {} void music_render(float *L, float *R, int n, double t) { (void)L; (void)R; (void)n; (void)t; }
void music_set_muted(bool m) { (void)m; }

`);
execSync('cc -O2 -DSFX_TEST -I src -I third_party src/sfx.c build/atest/comp_stubs.c build/atest/comp_main.c -o build/atest/comp -lm && build/atest/comp');
const cb = readFileSync('build/atest/comp_c.f32'); const cOut = new Float32Array(cb.buffer, cb.byteOffset, cb.length / 4);
let err = 0, ref = 0, maxd = 0, inE = 0;
for (let i = 0; i < chromeOut.length; i++) { const d = cOut[i] - chromeOut[i]; err += d * d; ref += chromeOut[i] ** 2; maxd = Math.max(maxd, Math.abs(d)); inE += inter[i] ** 2; }
const db = (x) => (10 * Math.log10(x)).toFixed(2);
console.log(`input ${db(inE / inter.length)} dBFS rms, chrome out ${db(ref / chromeOut.length)}, C out ${db(cOut.reduce((a, v) => a + v * v, 0) / cOut.length)}`);
console.log(`difference: ${db(err / ref)} dB below the signal, max |d| ${maxd.toFixed(5)}`);
// gain reduction envelopes per 10 ms
let worst = 0; for (let w = 0; w + 960 <= N; w += 480) { let a = 0, b = 0; for (let i = w; i < w + 480; i++) { a += chromeOut[i * 2] ** 2; b += cOut[i * 2] ** 2; } if (a > 1e-6) worst = Math.max(worst, Math.abs(10 * Math.log10(b / a))); }
console.log(`worst 10 ms window level difference ${worst.toFixed(3)} dB`);
