// Bakes full-body fighter cutouts for the station cover posters, from the game's own bodies.
//
//   OUT=build-poster EXTRA_CFLAGS="-DCK_POSTER -DCK_NOUI" ./build.sh
//   node tools/bake-poster.mjs build-poster/coffee-and-kill.wasc [outdir]
//
// The poster build (game.c CK_POSTER) shows one fighter alone, full body, mid-swing with its
// weapon, over black on frame i*8+6 and over white on frame i*8+7. The frame stores encoded
// colour, so an edge pixel is a*F + (1-a)*background in that space: the two backgrounds
// (measured in a corner, as tone mapping moves white) give the straight alpha and colour. The
// whole 1280x720 frame is kept (limbs mid-swing reach the edges), then auto-cropped to the
// fighter with a little padding, into assets/poster/<key>.png.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { deflateSync, crc32 } from 'node:zlib';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const WASMCART = join(ROOT, 'third_party', 'wasmcart');
const { CartHost } = await import(pathToFileURL(join(WASMCART, 'src', 'CartHost.js')));
const { createWebGL2Context } = await import(pathToFileURL(join(WASMCART, 'node_modules', 'webgl-node', 'index.mjs')));

function encodePng(rgba, w, h) {
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) Buffer.from(rgba.buffer, rgba.byteOffset + y * w * 4, w * 4).copy(raw, y * (w * 4 + 1) + 1);
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0))]);
}

const cart = process.argv[2];
if (!cart) { console.error('usage: node tools/bake-poster.mjs <poster build .wasc> [outdir]'); process.exit(2); }
const outDir = process.argv[3] || join(ROOT, 'assets', 'poster');
mkdirSync(outDir, { recursive: true });

const W = 1280, H = 720;
const { gl } = createWebGL2Context(W, H);
const host = new CartHost();
const keys = [];
const origLog = console.log, origErr = console.error;
console.error = (...a) => { const s = a.join(' '); const m = /\[poster\] (\d+) (\S+)/.exec(s); if (m) keys[+m[1]] = m[2]; else if (!/missing asset/.test(s)) origErr(...a); };
await host.load(new Uint8Array(readFileSync(cart)), { glBackend: gl, deterministic: { seed: 7 } });

const read = () => {
  const px = new Uint8Array(W * H * 4);
  host.withRenderedFrame(() => gl.readPixels(0, 0, W, H, gl.RGBA, gl.UNSIGNED_BYTE, px));
  return px;   // rows bottom up
};
const pads = [0, 1, 2, 3].map(() => ({ connected: false, buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0, leftTrigger: 0, rightTrigger: 0 }));

const STEP = 16;   // must match POSTER_STEP in game.c: black at STEP-2, white at STEP-1
let frame = -1, black = null, done = 0;
for (let guard = 0; guard < 40000; guard++) {
  const before = keys.length;
  host.runFrame(pads);
  await new Promise((r) => setImmediate(r));
  if (frame < 0) { if (!keys.length) continue; frame = 0; } else frame++;
  const i = Math.floor(frame / STEP), k = frame % STEP;
  if (k === 0 && frame > 0 && keys.length === before) break;   // no next fighter
  if (k === STEP - 2) black = read();
  if (k === STEP - 1) {
    const white = read();
    // backgrounds sampled at the top-left corner (bottom-up row 0 is the image bottom; a corner is empty)
    const bb = [black[0], black[1], black[2]], wb = [white[0], white[1], white[2]];
    const span = (wb[0] - bb[0]) + (wb[1] - bb[1]) + (wb[2] - bb[2]) || 765;
    // straight-alpha RGBA, top-down
    const full = new Uint8Array(W * H * 4);
    let minx = W, miny = H, maxx = -1, maxy = -1;
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const o = ((H - 1 - y) * W + x) * 4;   // flip to top-down
      const al = Math.max(0, Math.min(1, 1 - ((white[o] - black[o]) + (white[o + 1] - black[o + 1]) + (white[o + 2] - black[o + 2])) / span));
      const q = (y * W + x) * 4;
      const A = Math.round(al * 255);
      full[q + 3] = A;
      if (al > 0.004) {
        full[q] = Math.min(255, Math.round((black[o] - (1 - al) * bb[0]) / al));
        full[q + 1] = Math.min(255, Math.round((black[o + 1] - (1 - al) * bb[1]) / al));
        full[q + 2] = Math.min(255, Math.round((black[o + 2] - (1 - al) * bb[2]) / al));
      }
      if (A > 24) { if (x < minx) minx = x; if (x > maxx) maxx = x; if (y < miny) miny = y; if (y > maxy) maxy = y; }
    }
    if (maxx < 0) { origLog(`poster ${keys[i]}: empty, skipped`); continue; }
    const pad = 8;
    minx = Math.max(0, minx - pad); miny = Math.max(0, miny - pad);
    maxx = Math.min(W - 1, maxx + pad); maxy = Math.min(H - 1, maxy + pad);
    const cw = maxx - minx + 1, ch = maxy - miny + 1;
    const out = new Uint8Array(cw * ch * 4);
    for (let y = 0; y < ch; y++)
      Buffer.from(full.buffer, ((miny + y) * W + minx) * 4, cw * 4).copy(Buffer.from(out.buffer), y * cw * 4);
    writeFileSync(join(outDir, `${keys[i]}.png`), encodePng(out, cw, ch));
    origLog(`poster ${keys[i]}.png  ${cw}x${ch}`);
    done++;
  }
}
console.error = origErr;
console.log(`baked ${done} poster cutouts into ${outDir}`);
process.exit(0);
