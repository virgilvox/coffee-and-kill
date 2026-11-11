// Bakes the fighter select's portraits from the game's own bodies.
//
//   OUT=build-portraits EXTRA_CFLAGS="-DCK_PORTRAITS -DCK_NOUI" ./build.sh
//   node tools/bake-portraits.mjs build-portraits/coffee-and-kill.wasc
//
// The portrait build (game.c CK_PORTRAITS) shows fighter i alone, framed head and shoulders, over
// black on frame i*8+6 and over white on frame i*8+7. The frame stores encoded colour, so an edge
// pixel is a*F + (1-a)*background in that space: the two backgrounds (measured in a corner, as the
// scene's tone mapping moves white) give a = 1 - (white - black) / (white bg - black bg) and
// F = (black - (1-a) black bg) / a. The middle 560 x 560 of the 1280 x 720 frame, averaged down 2x, becomes
// assets/portraits/<key>.png (280 x 280, straight alpha). The loading card draws it at 212; the select
// averages it down to its cells when it loads (ui_select.c portraits_load).
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
if (!cart) { console.error('usage: node tools/bake-portraits.mjs <portrait build .wasc> [outdir]'); process.exit(2); }
const outDir = process.argv[3] || join(ROOT, 'assets', 'portraits');
mkdirSync(outDir, { recursive: true });

const W = 1280, H = 720, CROP = 560, SCALE = 2, N = CROP / SCALE;
const { gl } = createWebGL2Context(W, H);
const host = new CartHost();
const keys = [];
// the cart's log comes through console.error ([cart] ...): the portrait lines name each fighter
const origLog = console.log, origErr = console.error;
console.error = (...a) => { const s = a.join(' '); const m = /\[portrait\] (\d+) (\S+)/.exec(s); if (m) keys[+m[1]] = m[2]; else if (!/missing asset portrait_/.test(s)) origErr(...a); };
await host.load(new Uint8Array(readFileSync(cart)), { glBackend: gl, deterministic: { seed: 7 } });

const read = () => {
  const px = new Uint8Array(W * H * 4);
  host.withRenderedFrame(() => gl.readPixels(0, 0, W, H, gl.RGBA, gl.UNSIGNED_BYTE, px));
  return px;   // rows bottom up
};
const pads = [0, 1, 2, 3].map(() => ({ connected: false, buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0, leftTrigger: 0, rightTrigger: 0 }));
// boot until the cart logs its first fighter, then 8 frames per fighter until none follows
let frame = -1, black = null, done = 0;
for (let guard = 0; guard < 6000; guard++) {
  const before = keys.length;
  host.runFrame(pads);
  await new Promise((r) => setImmediate(r));
  if (frame < 0) { if (!keys.length) continue; frame = 0; } else frame++;
  const i = Math.floor(frame / 8), k = frame % 8;
  if (k === 0 && frame > 0 && keys.length === before) break;   // no next fighter
  if (k === 6) black = read();
  if (k === 7) {
    const white = read(), out = new Uint8Array(N * N * 4);
    const x0 = (W - CROP) / 2, y0 = (H - CROP) / 2;
    const bb = [black[0], black[1], black[2]], wb = [white[0], white[1], white[2]];
    const span = (wb[0] - bb[0]) + (wb[1] - bb[1]) + (wb[2] - bb[2]);
    for (let y = 0; y < N; y++) for (let x = 0; x < N; x++) {
      let r = 0, g = 0, b = 0, a = 0;
      for (let sy = 0; sy < SCALE; sy++) for (let sx = 0; sx < SCALE; sx++) {
        const o = ((H - 1 - (y0 + y * SCALE + sy)) * W + x0 + x * SCALE + sx) * 4;
        const al = Math.max(0, Math.min(1, 1 - ((white[o] - black[o]) + (white[o + 1] - black[o + 1]) + (white[o + 2] - black[o + 2])) / span));
        a += al;   // over black: the colour times its alpha, plus what is left of the background
        r += black[o] - (1 - al) * bb[0]; g += black[o + 1] - (1 - al) * bb[1]; b += black[o + 2] - (1 - al) * bb[2];
      }
      const q = (y * N + x) * 4, n = SCALE * SCALE, A = a / n;
      out[q + 3] = Math.round(A * 255);
      if (A > 0.004) { out[q] = Math.min(255, Math.round(r / n / A)); out[q + 1] = Math.min(255, Math.round(g / n / A)); out[q + 2] = Math.min(255, Math.round(b / n / A)); }
    }
    writeFileSync(join(outDir, `${keys[i]}.png`), encodePng(out, N, N));
    origLog(`portrait ${keys[i]}.png`);
    done++;
  }
}
console.error = origErr;
console.log(`baked ${done} portraits into ${outDir}`);
process.exit(0);
