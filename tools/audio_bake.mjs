// Bakes COFFEE & KILL's music with the page's own Bellows: the AudioWorklet
// kernel the page ships (run here in Node), driven by the page's own score
// code (Dl, wl, gl, il, j0, rl and the songs of $l / P3), through the page's
// Transport and Scheduler, with a stand-in for the Gr class that posts the
// same messages Gr posts. What comes out is what the page's AudioWorkletNode
// would output with masterGain 1.
//
//   node tools/audio_bake.mjs <coffee-and-kill.html> <outdir> [--rate 48000]
//
// Writes <outdir>/fight0.f32 fight1.f32 fight2.f32 (one steady loop each of
// il, gl and wl: 128, 128 and 256 sixteenths at 140 bpm with swing .03) and
// dance_<id>.f32 for the five songs (from the song's first step), raw
// interleaved stereo float32, plus bake.json with the loop and song lengths.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import vm from 'node:vm';

const args = process.argv.slice(2);
const [html, out] = args;
const RATE = args.includes('--rate') ? +args[args.indexOf('--rate') + 1] : 48000;
mkdirSync(out, { recursive: true });

// ---- the page's code, out of the bundle ----
const page = readFileSync(html, 'utf8');
const s0 = page.indexOf('<script>', page.indexOf('</style>')), s1 = page.indexOf('</script>', s0);
const bundle = page.slice(s0 + 8, s1);
function strLit(t, i) {      // the JS string literal starting at t[i]
  const q = t[i]; let j = i + 1;
  while (t[j] !== q) j += t[j] === '\\' ? 2 : 1;
  return eval(t.slice(i, j + 1));
}
const kernelSrc = strLit(bundle, bundle.indexOf('\'"use strict";(()=>{function ii(a)'));

// the pretty printed bundle is easier to slice by declaration; esbuild made it
const pretty = readFileSync(new URL('../build/audio-src/pretty.js', import.meta.url), 'utf8').split('\n');
const game = readFileSync(new URL('../build/audio-src/game.js', import.meta.url), 'utf8').split('\n');
const lines = (arr, a, b) => arr.slice(a - 1, b).join('\n');

// Transport (Tr), its tempo map (ao), Scheduler (co), the time parser (or) and the note parser (hQ)
const transportSrc = lines(pretty, 15360, 15619).replace(/^  \}, gm = 0\.25, co = class/, '  var gm = 0.25, co = class');
const T = Function('C', `${transportSrc}\n return { co, Tr, or, hQ };`)(
  (o, k, v) => Object.defineProperty(o, k, { value: v, writable: true, enumerable: true, configurable: true }));

// ---- the kernel, as the AudioWorkletGlobalScope runs it ----
let Proc = null;
const scope = {
  sampleRate: RATE, currentFrame: 0, console, Math, Float32Array, Float64Array, Int32Array, Uint8Array, Uint32Array, Int16Array,
  Map, Set, Array, Object, Number, String, Error, Function, JSON, isFinite, Infinity, NaN, Symbol,
  AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
  registerProcessor: (n, c) => { Proc = c; },
};
vm.createContext(scope);
vm.runInContext(kernelSrc, scope);

// ---- Gr, as far as the game uses it ----
class Band {
  constructor(seed, bpm) {
    this.proc = new Proc();
    this.seed = seed;
    this.frame = 0;
    this.transport = new T.Tr({ bpm });
    this.scheduler = new T.co(this.transport);
    this.nextChannel = 0; this.nextBus = 1; this.nextNote = 1;
    this.liveRng = new Map();
    const self = this;
    this.clock = { at: (sub, cb) => self.scheduler.at(T.or(sub), cb) };
    this.L = new Float32Array(128); this.R = new Float32Array(128);
    this.post({ type: 'masterGain', gain: 0 });   // Gr.boot({ masterGain: 0 })
  }
  get now() { return this.frame / RATE; }
  post(m) { if (LOG) LOG.push({ t: this.now, m: structuredClone(m) }); this.proc.port.onmessage({ data: structuredClone(m) }); }
  rng(k) { let r = this.liveRng.get(k); if (!r) { r = scope.__T ? null : null; } return r; }
  voice(engineId, params = {}, opts) {
    const id = this.nextChannel++;
    this.post({ type: 'createChannel', id, engineId, params, seed: this.seed + '::ch' + id, polyphony: opts?.polyphony });
    return new Voice(this, id);
  }
  bus(chain, opts) {
    const id = this.nextBus++;
    this.post({ type: 'createBus', id, chain: pB(chain), returnLevel: opts?.level ?? 1 });
    return { id };
  }
  masterFx(...chain) { this.post({ type: 'masterFx', chain: pB(chain) }); }
  masterGain(g) { this.post({ type: 'masterGain', gain: g }); }
  panic() { this.post({ type: 'panic' }); }
  start() { this.scheduler.rewind(); this.transport.start(this.now + 0.05); }
  bpm(b) { this.transport.setBpm(b, this.now); }
  swing(a, sub = '8n') { this.transport.setSwing(a, T.or(sub)); }
  freqOf(n) { const m = typeof n === 'string' ? T.hQ(n) : n; return 440 * Math.pow(2, (m - 69) / 12); }
  durationSeconds(d, at) {
    if (typeof d === 'object') return d.seconds;
    const e = T.or(d), t = this.transport;
    if (t.state !== 'running') return t.tempo.beatToSeconds(e);
    const D = t.beatAt(at);
    return t.secondsAt(D + e) - at;
  }
  noteEvents(ch, n, o) {
    const at = o.at ?? this.now + 0.005, dur = this.durationSeconds(o.dur ?? '8n', at);
    const vel = Math.min(1, Math.max(0, o.vel ?? 0.8)), freq = this.freqOf(n), id = this.nextNote++;
    this.post({ type: 'events', events: [{ time: at, kind: 0, target: ch, a: id, b: freq, c: vel }, { time: at + dur, kind: 1, target: ch, a: id, b: 0, c: 0 }] });
  }
  // the page's setInterval(25 ms) tick, and the worklet's 128 frame blocks
  render(seconds, sink) {
    const blocks = Math.ceil(seconds * RATE / 128);
    for (let b = 0; b < blocks; b++) {
      if (this.transport.state === 'running' && (this.nextTick === undefined || this.now >= this.nextTick)) {
        this.scheduler.tick(this.now); this.nextTick = (this.nextTick ?? this.now) + 0.025;
      }
      scope.currentFrame = this.frame;
      this.proc.process([], [[this.L, this.R]]);
      sink(this.L, this.R, this.frame);
      this.frame += 128;
    }
  }
}
class Voice {
  constructor(b, id) { this.b = b; this.channel = id; }
  note(n, o = {}) { this.b.noteEvents(this.channel, n, o); return this; }
  chord(ns, o = {}) { for (const n of ns) this.b.noteEvents(this.channel, n, o); return this; }
  fx(...c) { this.b.post({ type: 'channelFx', id: this.channel, chain: pB(c) }); return this; }
  send(bus, level) { this.b.post({ type: 'send', channelId: this.channel, busId: bus.id, level }); return this; }
  gain(g) { this.b.post({ type: 'channelGain', id: this.channel, gain: g }); return this; }
  pan(p) { this.b.post({ type: 'channelPan', id: this.channel, pan: p }); return this; }
}
function pB(s) { return s.map((A) => typeof A === 'string' ? { effectId: A, params: {} } : Array.isArray(A) ? { effectId: A[0], params: A[1] } : A); }

// ---- the game's score (game.js lines 1093..1242) with hooks into its module state ----
const scoreSrc = lines(game, 1093, 1242);
const songSrc = lines(game, 2916, 3039);
function loadScore(band) {
  const f = Function('vP_in', 'Ot', `${scoreSrc}\n${songSrc}\n
    vP = vP_in; vP.rng = (k) => Ot(vP.seed + '::' + k);
    return {
      Dl, j0, rl, HC, P3, K0,
      set Sw(v) { Sw = v; }, get Sw() { return Sw; },
      set U8(v) { U8 = v; }, set Zr(v) { Zr = v; }, set JB(v) { JB = v; }, set u0(v) { u0 = v; },
      get T0() { return T0; }, set ND(v) { ND = v; },
    };`);
  return f(band, Ot);
}
// the page's Ot (seeded rng), for vP.rng
function Ot(s) {
  let A = 1779033703 ^ s.length;
  for (let P = 0; P < s.length; P++) A = Math.imul(A ^ s.charCodeAt(P), 3432918353), A = A << 13 | A >>> 19;
  let h = (() => { A = Math.imul(A ^ A >>> 16, 2246822507); A = Math.imul(A ^ A >>> 13, 3266489909); return (A ^= A >>> 16) >>> 0; })();
  let st = h;
  const r = () => { st |= 0; st = st + 1831565813 | 0; let t = Math.imul(st ^ st >>> 15, 1 | st); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; };
  r.fork = (e) => Ot(s + '::' + e);
  return r;
}

const LOG = process.env.LOG_LEVEL !== undefined ? [] : null;
if (LOG) {   // LOG_LEVEL=<sw> LOG_SECS=<s> LOG_OUT=<file>: the messages of the fight score at that level, then exit
  const band = new Band('coffee-and-kill', 140);
  const S = loadScore(band);
  S.Sw = +process.env.LOG_LEVEL;
  S.Dl();
  band.render(+(process.env.LOG_SECS || 10), () => {});
  writeFileSync(process.env.LOG_OUT, JSON.stringify({ rate: RATE, msgs: LOG }));
  process.exit(0);
}
const SPS = 60 / 140 / 4;          // seconds per sixteenth at 140
const meta = { rate: RATE, sps: SPS, fight: [], dance: [] };

function writeF32(name, L, R, a, b) {
  const n = b - a, buf = new Float32Array(n * 2);
  for (let i = 0; i < n; i++) { buf[i * 2] = L[a + i]; buf[i * 2 + 1] = R[a + i]; }
  writeFileSync(join(out, name), Buffer.from(buf.buffer));
}
function collect(band, seconds) {
  const n = Math.ceil(seconds * RATE / 128) * 128, L = new Float32Array(n), R = new Float32Array(n);
  band.render(seconds, (l, r, f) => { L.set(l, f); R.set(r, f); });
  return { L, R };
}

// ---- the fight loops: Dl, the level held in one band, two loops, keep the second ----
for (const [lvl, sw, steps] of [[0, 0, 128], [1, 0.35, 128], [2, 1, 256]]) {
  const band = new Band('coffee-and-kill', 140);
  const S = loadScore(band);
  S.Sw = sw;
  S.Dl();                     // buses, master fx, voices, bpm 140, swing .03, the clock, start
  band.masterGain(1);
  const loopS = steps * SPS, t0 = 0.05;        // transport.start(now + 0.05)
  const { L, R } = collect(band, t0 + loopS * 2 + 0.1);
  const a = Math.round((t0 + loopS) * RATE), b = Math.round((t0 + 2 * loopS) * RATE);
  writeF32(`fight${lvl}.f32`, L, R, a, b);
  meta.fight.push({ level: lvl, steps, seconds: loopS, frames: b - a });
  console.log('fight', lvl, steps, 'steps', (b - a), 'frames');
}

// ---- the dance songs: the fight setup (channel ids follow it), then tt.dance(true, song) ----
for (let k = 0; k < 5; k++) {
  const band = new Band('coffee-and-kill', 140);
  const S = loadScore(band);
  S.Sw = 0;
  S.Dl();
  const song = S.HC[k];
  // tt.dance(true, A): panic, u0 = A, SB, j0(A), U8, Zr, JB = 0, bpm, swing 0; the next 16n tick starts it
  band.panic();
  S.u0 = song; S.j0(song); S.U8 = true; S.Zr = true; S.JB = 0;
  let started = null; S.ND = (t) => { started = t; };
  band.bpm(song.bpm); band.swing(0, '16n');
  band.masterGain(1);
  const songS = song.totalSteps * song.secPerStep;
  const { L, R } = collect(band, 1 + songS + 5);
  const a = Math.round(started * RATE);
  // the song's end: the last step plus the longest tail that is still audible (-70 dB)
  let end = L.length;
  while (end > a && Math.max(Math.abs(L[end - 1]), Math.abs(R[end - 1])) < 3e-4) end--;
  end = Math.min(L.length, end + 256);
  writeF32(`dance_${song.id}.f32`, L, R, a, end);
  meta.dance.push({ idx: k, id: song.id, bpm: song.bpm, steps: song.totalSteps, sps: song.secPerStep, start: started, frames: end - a });
  console.log('dance', song.id, song.totalSteps, 'steps', 'starts at', started.toFixed(4), 'frames', end - a);
}
writeFileSync(join(out, 'bake.json'), JSON.stringify(meta, null, 1));
