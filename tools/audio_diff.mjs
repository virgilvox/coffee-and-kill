// Aligns a stretch of the cart's WAV with a recording of the page and
// compares them: the lag by cross-correlation (coarse on 1 ms RMS envelopes,
// then sample exact on the waveform), then the level difference per 50 ms and
// the residual after alignment.
//   node tools/audio_diff.mjs page.f32 cart.wav <cart from s> <cart to s>
import { readFileSync } from 'node:fs';
const [pf, cf, a, b] = process.argv.slice(2);
const R = 48000;
const pb = readFileSync(pf); const P = new Float32Array(pb.buffer, pb.byteOffset, pb.length / 4);
const wb = readFileSync(cf); const C = new Float32Array((wb.length - 44) / 2); for (let i = 0; i < C.length; i++) C[i] = wb.readInt16LE(44 + i * 2) / 32767;
const mono = (x, s, n) => { const y = new Float32Array(n); for (let i = 0; i < n; i++) y[i] = (x[(s + i) * 2] + x[(s + i) * 2 + 1]) / 2; return y; };
const c0 = Math.round(+a * R), n = Math.round((+b - +a) * R);
const cm = mono(C, c0, n), pm = mono(P, 0, P.length / 2);
const env = (x, w) => { const m = Math.floor(x.length / w), e = new Float32Array(m); for (let k = 0; k < m; k++) { let s = 0; for (let i = 0; i < w; i++) s += x[k * w + i] ** 2; e[k] = Math.sqrt(s / w); } return e; };
const W = 48, ce = env(cm, W), pe = env(pm, W);
let best = -1, bl = 0;
for (let l = 0; l + ce.length <= pe.length; l++) { let s = 0, s1 = 0, s2 = 0; for (let i = 0; i < ce.length; i++) { s += ce[i] * pe[l + i]; s1 += ce[i] ** 2; s2 += pe[l + i] ** 2; } const r = s / Math.sqrt(s1 * s2 + 1e-12); if (r > best) { best = r; bl = l; } }
let fine = bl * W, bestf = -1;
for (let l = Math.max(0, bl * W - 2 * W); l <= bl * W + 2 * W && l + n <= pm.length; l++) { let s = 0, s1 = 0, s2 = 0; for (let i = 0; i < n; i += 3) { s += cm[i] * pm[l + i]; s1 += cm[i] ** 2; s2 += pm[l + i] ** 2; } const r = s / Math.sqrt(s1 * s2 + 1e-12); if (r > bestf) { bestf = r; fine = l; } }
let e = 0, ref = 0, cr = 0; const win = 2400; let worst = 0, sumAbs = 0, nw = 0;
for (let k = 0; k + win <= n; k += win) { let x = 0, y = 0; for (let i = k; i < k + win; i++) { x += cm[i] ** 2; y += pm[fine + i] ** 2; } if (y > 1e-7) { const d = 10 * Math.log10((x + 1e-12) / y); worst = Math.max(worst, Math.abs(d)); sumAbs += Math.abs(d); nw++; } }
for (let i = 0; i < n; i++) { e += (cm[i] - pm[fine + i]) ** 2; ref += pm[fine + i] ** 2; cr += cm[i] ** 2; }
console.log(`aligned at page +${(fine / R).toFixed(4)} s (envelope r ${best.toFixed(4)}, waveform r ${bestf.toFixed(4)})`);
console.log(`level: page ${(10 * Math.log10(ref / n)).toFixed(2)} dB rms, cart ${(10 * Math.log10(cr / n)).toFixed(2)} dB rms`);
console.log(`per 50 ms: mean |level diff| ${(sumAbs / nw).toFixed(2)} dB, worst ${worst.toFixed(2)} dB; residual ${(10 * Math.log10(e / ref)).toFixed(1)} dB below the page`);
