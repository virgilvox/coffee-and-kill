// Bakes the stage select's stills from the game's own stages.
//
//   OUT=build-stills EXTRA_CFLAGS="-DCK_STILLS -DCK_NOUI" ./build.sh
//   node tools/bake-stills.mjs build-stills/coffee-and-kill.wasc
//
// The stills build (game.c CK_STILLS) loads each stage in turn, puts the stage select's camera on
// it and logs "[still] <index> <key>" on the frame to keep. That 1280 x 720 frame, averaged down
// 2x, becomes assets/stills/<key>.jpg (640 x 360), through ffmpeg. The select shows a still large
// for a stage that is not loaded yet, and small in its strip for every stage.
import { readFileSync, writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFileSync } from 'node:child_process';
import { deflateSync, crc32 } from 'node:zlib';
import { tmpdir } from 'node:os';

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
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

const cart = process.argv[2];
if (!cart) { console.error('usage: node tools/bake-stills.mjs <stills build .wasc> [outdir]'); process.exit(2); }
const outDir = process.argv[3] || join(ROOT, 'assets', 'stills');
mkdirSync(outDir, { recursive: true });
const tmp = join(tmpdir(), `ck-stills-${process.pid}`);
mkdirSync(tmp, { recursive: true });

const W = 1280, H = 720, N = 2, OW = W / N, OH = H / N;
const { gl } = createWebGL2Context(W, H);
const host = new CartHost();
// the cart's log comes through console.error: the still lines name each stage
const origErr = console.error;
let pending = null, total = null;
console.error = (...a) => {
  const s = a.join(' '), m = /\[still\] (\d+) (\S+)/.exec(s);
  if (m) pending = { i: +m[1], key: m[2] };
  else if (!/missing asset (still|portrait)_/.test(s)) origErr(...a);
};
await host.load(new Uint8Array(readFileSync(cart)), { glBackend: gl, deterministic: { seed: 7 } });
const pads = [0, 1, 2, 3].map(() => ({ connected: false, buttons: 0, leftX: 0, leftY: 0, rightX: 0, rightY: 0, leftTrigger: 0, rightTrigger: 0 }));
let done = 0, idle = 0;
for (let guard = 0; guard < 4000 && idle < 600; guard++) {
  host.runFrame(pads);
  await new Promise((r) => setImmediate(r));
  if (!pending) { idle++; continue; }
  idle = 0;
  const px = new Uint8Array(W * H * 4);
  host.withRenderedFrame(() => gl.readPixels(0, 0, W, H, gl.RGBA, gl.UNSIGNED_BYTE, px));
  const out = new Uint8Array(OW * OH * 4);
  for (let y = 0; y < OH; y++) for (let x = 0; x < OW; x++) {
    let r = 0, g = 0, b = 0;
    for (let sy = 0; sy < N; sy++) for (let sx = 0; sx < N; sx++) {
      const o = ((H - 1 - (y * N + sy)) * W + x * N + sx) * 4;   // GL rows are bottom up
      r += px[o]; g += px[o + 1]; b += px[o + 2];
    }
    const q = (y * OW + x) * 4;
    out[q] = r / (N * N); out[q + 1] = g / (N * N); out[q + 2] = b / (N * N); out[q + 3] = 255;
  }
  const png = join(tmp, `${pending.key}.png`);
  writeFileSync(png, encodePng(out, OW, OH));
  execFileSync('ffmpeg', ['-loglevel', 'error', '-y', '-i', png, '-q:v', '3', join(outDir, `${pending.key}.jpg`)]);
  console.log(`still ${pending.key}.jpg`);
  done++; pending = null;
}
console.error = origErr;
rmSync(tmp, { recursive: true, force: true });
console.log(`baked ${done} stills into ${outDir}`);
process.exit(0);
