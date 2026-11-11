// Bakes assets/anim/rig_vroid.bin: the clips the KEYHOLDER fighters play (VRoid bodies, humanoid
// bone names, identity rests, +z facing). It holds:
//   - KEYHOLDER's own clips (its assets/anim/rig.bin, Quaternius UAL retargeted by DOLLY), the ones
//     this game uses: idles, hits, the sword combos, the specials' jumps, casts and dashes
//   - every COFFEE & KILL clip KEYHOLDER lacks (the light and heavy chains, the kick, the ground
//     pound, the dodges, the strafes, the celebration), retargeted here from the page's rig_a.bin
//
// Both rigs carry the same UAL motion, so the clips they share fix the retarget: for each bone the
// target's world rotation is the source's times a constant offset, A_t(b) = A_s(b) * O(b). O is
// averaged over every frame of every shared clip and its spread is printed (it should be a few
// degrees at most). Fingers and other bones the page's rig lacks hold KEYHOLDER's Sword_Idle grip.
// The hips track is converted the same way: an offset from the T pose in units of hip height.
//
//   node tools/rig-vroid.mjs [path/to/keyholder]      (default ../keyholder)
//
// Output format (CKAN version 2, anim.c): "CKAN" u32 2, u32 nbones, names, u32 nclips; per clip
// u8 len + name, f32 dur, u32 nf, f32[8] (unused windows), i16 quats[nf*nb*4], f32 hips[nf*3]
// (offsets in hip heights), f32 ground speed (hip heights a second).
import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const KH = resolve(process.argv[2] || join(ROOT, '..', 'keyholder'));

// ---- quaternions {x, y, z, w} as arrays ----
const qmul = (a, b) => [
  a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
  a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
  a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]];
const qinv = (q) => [-q[0], -q[1], -q[2], q[3]];
const qnorm = (q) => { const l = Math.hypot(...q) || 1; return q.map((v) => v / l); };
const qdot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
const qangle = (a, b) => 2 * Math.acos(Math.min(1, Math.abs(qdot(a, b)))) * 180 / Math.PI;
const qrot = (q, v) => { const p = qmul(qmul(q, [v[0], v[1], v[2], 0]), qinv(q)); return [p[0], p[1], p[2]]; };
const ID = [0, 0, 0, 1];

// ---- the two rig formats ----
function readRig(path, kh) {
  const b = readFileSync(path);
  if (b.toString('latin1', 0, 4) !== 'CKAN') throw new Error('not a rig: ' + path);
  let p = 8;
  const u32 = () => { const v = b.readUInt32LE(p); p += 4; return v; };
  const f32 = () => { const v = b.readFloatLE(p); p += 4; return v; };
  const str = () => { const n = b[p++]; const s = b.toString('utf8', p, p + n); p += n; return s; };
  const nb = u32(), bones = []; for (let i = 0; i < nb; i++) bones.push(str());
  const nc = u32(), clips = {};
  for (let c = 0; c < nc; c++) {
    const name = str(), dur = f32(), nf = u32();
    for (let i = 0; i < 8; i++) f32();
    const q = [];
    for (let f = 0; f < nf; f++) {
      const row = [];
      for (let i = 0; i < nb; i++) { const o = p + ((f * nb + i) * 4) * 2; row.push(qnorm([0, 1, 2, 3].map((k) => b.readInt16LE(o + k * 2) / 32767))); }
      q.push(row);
    }
    p += nf * nb * 8;
    const hips = []; for (let f = 0; f < nf; f++) { hips.push([b.readFloatLE(p), b.readFloatLE(p + 4), b.readFloatLE(p + 8)]); p += 12; }
    const speed = kh ? f32() : 0;
    clips[name] = { name, dur, nf, q, hips, speed };
  }
  return { bones, clips, order: Object.keys(clips) };
}
function glbNodes(path) {
  const b = readFileSync(path), jl = b.readUInt32LE(12);
  const J = JSON.parse(b.subarray(20, 20 + jl).toString());
  const parent = {}, rot = {};
  J.nodes.forEach((n, i) => { rot[n.name] = n.rotation || ID; (n.children || []).forEach((c) => { parent[J.nodes[c].name] = n.name; }); });
  return { parent, rot };
}

const SRC = readRig(join(ROOT, 'assets/anim/rig_a.bin'), false);
const TGT = readRig(join(KH, 'assets/anim/rig.bin'), true);
const sN = glbNodes(join(ROOT, 'assets/models/char_a.glb'));
const tN = glbNodes(join(KH, 'assets/models/char_c.glb'));

// the page's mannequin to the VRM humanoid
const MAP = {
  Hips: 'hips', Spine02: 'spine', Spine01: 'chest', Spine: 'upperChest', neck: 'neck', Head: 'head',
  LeftShoulder: 'leftShoulder', LeftArm: 'leftUpperArm', LeftForeArm: 'leftLowerArm', LeftHand: 'leftHand',
  RightShoulder: 'rightShoulder', RightArm: 'rightUpperArm', RightForeArm: 'rightLowerArm', RightHand: 'rightHand',
  LeftUpLeg: 'leftUpperLeg', LeftLeg: 'leftLowerLeg', LeftFoot: 'leftFoot', LeftToeBase: 'leftToes',
  RightUpLeg: 'rightUpperLeg', RightLeg: 'rightLowerLeg', RightFoot: 'rightFoot', RightToeBase: 'rightToes',
};
const BACK = Object.fromEntries(Object.entries(MAP).map(([a, b]) => [b, a]));

// world rotations of one frame: the source above Hips carries the glb's Armature turn
const sIdx = Object.fromEntries(SRC.bones.map((n, i) => [n, i]));
const tIdx = Object.fromEntries(TGT.bones.map((n, i) => [n, i]));
const sRoot = (() => { let q = ID, n = sN.parent.Hips; while (n) { q = qmul(sN.rot[n], q); n = sN.parent[n]; } return q; })();
function worldS(row) {
  const W = {};
  const get = (n) => {
    if (W[n]) return W[n];
    const par = sN.parent[n];
    const pw = n === 'Hips' ? sRoot : get(par);
    return (W[n] = qmul(pw, row[sIdx[n]]));
  };
  for (const n of SRC.bones) get(n);
  return W;
}
function worldT(row) {
  const W = {};
  const get = (n) => {
    if (W[n]) return W[n];
    const par = tN.parent[n];
    const pw = par && tIdx[par] !== undefined ? get(par) : (par ? get(par) : ID);
    return (W[n] = qmul(pw, tIdx[n] !== undefined ? row[tIdx[n]] : ID));
  };
  for (const n of TGT.bones) get(n);
  return W;
}

// ---- calibrate O(b) over the shared clips ----
const shared = SRC.order.filter((c) => TGT.clips[c] && TGT.clips[c].nf === SRC.clips[c].nf && !/^A_TPose$/.test(c));
const acc = {};
for (const c of shared) {
  const S = SRC.clips[c], T = TGT.clips[c];
  for (let f = 0; f < S.nf; f++) {
    const ws = worldS(S.q[f]), wt = worldT(T.q[f]);
    for (const [s, t] of Object.entries(MAP)) {
      const o = qmul(qinv(ws[s]), wt[t]);
      const a = acc[t] || (acc[t] = { sum: [0, 0, 0, 0], ref: o, all: [] });
      const k = qdot(a.ref, o) < 0 ? -1 : 1;
      for (let i = 0; i < 4; i++) a.sum[i] += o[i] * k;
      a.all.push(o);
    }
  }
}
const O = {};
let worst = 0;
for (const [t, a] of Object.entries(acc)) {
  O[t] = qnorm(a.sum);
  const dev = a.all.map((o) => qangle(o, O[t])).sort((x, y) => x - y);
  const p95 = dev[Math.floor(dev.length * 0.95)];
  worst = Math.max(worst, p95);
  if (process.env.VERBOSE) console.log(`  ${t.padEnd(14)} p50 ${dev[dev.length >> 1].toFixed(2)} p95 ${p95.toFixed(2)} deg`);
}
console.log(`retarget: ${shared.length} shared clips, offset spread p95 worst ${worst.toFixed(2)} deg`);

// ---- hips: offsets from the T pose in hip heights, in the target's frame ----
// the source's hips world position: the Armature turn applied to the Hips track
const tpose = SRC.clips.A_TPose;
const hipsW = (p) => qrot(sRoot, p);
const rest = hipsW(tpose.hips[0]);
// the two rigs face the same way (both +z) and differ in scale and rest: fit target = a * (source - rest) + b
// per axis over the shared clips (their last frames left out: KEYHOLDER's bake holds a one shot's last frame)
const fit = [0, 1, 2].map((k) => {
  let sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
  for (const c of shared) {
    const S = SRC.clips[c], T = TGT.clips[c];
    for (let f = 0; f < S.nf - 1; f++) { const x = hipsW(S.hips[f])[k] - rest[k], y = T.hips[f][k]; sx += x; sy += y; sxx += x * x; sxy += x * y; n++; }
  }
  const a = (n * sxy - sx * sy) / (n * sxx - sx * sx);
  return { a, b: (sy - a * sx) / n };
});
const hipsT = (p) => { const w = hipsW(p); return [0, 1, 2].map((k) => fit[k].a * (w[k] - rest[k]) + fit[k].b); };
let herr = 0, hn = 0;
for (const c of shared) {
  const S = SRC.clips[c], T = TGT.clips[c];
  for (let f = 0; f < S.nf - 1; f++) { const h = hipsT(S.hips[f]); herr += Math.hypot(h[0] - T.hips[f][0], h[1] - T.hips[f][1], h[2] - T.hips[f][2]); hn++; }
}
console.log(`hips: scale ${fit.map((f) => f.a.toFixed(3)).join(' ')}, mean error ${(herr / hn).toFixed(4)} hip heights`);

// the grip: fingers and anything else the page's rig lacks, from KEYHOLDER's Sword_Idle
const grip = TGT.clips.Sword_Idle.q[0];

function retarget(name) {
  const S = SRC.clips[name];
  const q = [], hips = [];
  for (let f = 0; f < S.nf; f++) {
    const ws = worldS(S.q[f]);
    const wt = {};
    for (const [s, t] of Object.entries(MAP)) wt[t] = qmul(ws[s], O[t]);
    const row = TGT.bones.map((b, i) => {
      if (!wt[b]) return grip[i];
      // the parent's world: the nearest mapped ancestor (the chain between is held at identity)
      let par = tN.parent[b];
      while (par && !wt[par]) par = tN.parent[par];
      return qnorm(qmul(par ? qinv(wt[par]) : ID, wt[b]));
    });
    q.push(row);
    hips.push(hipsT(S.hips[f]));
  }
  // ground speed: the hips' mean horizontal travel
  const a = hips[0], z = hips[hips.length - 1];
  const speed = S.dur > 0 ? Math.hypot(z[0] - a[0], z[2] - a[2]) / S.dur : 0;
  return { name, dur: S.dur, nf: S.nf, q, hips, speed };
}

// a check: retarget the shared clips too and compare with KEYHOLDER's own bake
let cerr = 0, cn = 0;
for (const c of shared) {
  const R = retarget(c), T = TGT.clips[c];
  for (let f = 0; f < R.nf; f++) for (const t of Object.values(MAP)) { cerr += qangle(R.q[f][tIdx[t]], T.q[f][tIdx[t]]); cn++; }
}
console.log(`check: shared clips retargeted against KEYHOLDER's bake, mean ${(cerr / cn).toFixed(2)} deg per bone`);

// KEYHOLDER's clips this game uses, then the page's clips KEYHOLDER lacks
const KEEP_KH = `Idle_Loop Idle_FoldArms_Loop Idle_No_Loop Jog_Fwd_Loop Sprint_Loop Jump_Start Jump_Loop Jump_Land
 NinjaJump_Start NinjaJump_Idle_Loop NinjaJump_Land Roll Slide_Start Slide_Loop Slide_Exit Hit_Chest Hit_Head Hit_Knockback
 Death01 LayToIdle Sword_Idle Sword_Regular_A Sword_Regular_A_Rec Sword_Regular_B Sword_Regular_B_Rec Sword_Regular_C
 Sword_Regular_Combo Sword_Heavy_Combo Sword_Dash Sword_Block Sword_Attack Idle_Shield_Loop Shield_Dash Shield_OneShot
 Idle_Shield_Break Spell_Simple_Enter Spell_Simple_Idle_Loop Spell_Simple_Shoot Spell_Simple_Exit Punch_Jab Punch_Cross
 Melee_Hook Melee_Hook_Rec OverhandThrow Yes Dance_Loop`.split(/\s+/);
const out = [];
for (const c of KEEP_KH) { if (!TGT.clips[c]) throw new Error('KEYHOLDER has no clip ' + c); out.push(TGT.clips[c]); }
const added = [];
for (const c of SRC.order) if (!TGT.clips[c] && c !== 'A_TPose') { out.push(retarget(c)); added.push(c); }

const chunks = [];
const u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v); chunks.push(b); };
const f32 = (v) => { const b = Buffer.alloc(4); b.writeFloatLE(v); chunks.push(b); };
const str = (s) => { chunks.push(Buffer.from([s.length]), Buffer.from(s)); };
chunks.push(Buffer.from('CKAN')); u32(2); u32(TGT.bones.length); TGT.bones.forEach(str);
u32(out.length);
for (const c of out) {
  str(c.name); f32(c.dur); u32(c.nf);
  for (let i = 0; i < 8; i++) f32(0);
  const q = Buffer.alloc(c.nf * TGT.bones.length * 8);
  for (let f = 0; f < c.nf; f++) c.q[f].forEach((v, i) => { const o = (f * TGT.bones.length + i) * 8; for (let k = 0; k < 4; k++) q.writeInt16LE(Math.round(Math.max(-1, Math.min(1, v[k])) * 32767), o + k * 2); });
  chunks.push(q);
  const h = Buffer.alloc(c.nf * 12);
  c.hips.forEach((p, f) => { for (let k = 0; k < 3; k++) h.writeFloatLE(p[k], f * 12 + k * 4); });
  chunks.push(h);
  f32(c.speed || 0);
}
const buf = Buffer.concat(chunks);
writeFileSync(join(ROOT, 'assets/anim/rig_vroid.bin'), buf);
console.log(`rig_vroid.bin: ${out.length} clips (${KEEP_KH.length} from KEYHOLDER, ${added.length} retargeted), ${TGT.bones.length} bones, ${(buf.length / 1048576).toFixed(2)} MB`);
console.log(`retargeted: ${added.join(' ')}`);
