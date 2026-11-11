// Records the real page's final output (after its DynamicsCompressor) in
// headless Chrome for N seconds of the title screen: a ScriptProcessor taps
// the compressor that the page connects to its destination.
//   node tools/audio_record.mjs <coffee-and-kill.html> <out.f32> [seconds]
// Writes interleaved stereo float32 at the context rate, from the moment the tap connects.
import { writeFileSync } from 'node:fs';
import { chrome } from './audio_cdp.mjs';
const [html, out, secs = '20'] = process.argv.slice(2);
const c = await chrome({ port: 9337 });
await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `
  window.__rec = []; window.__recOn = false;
  const conn = AudioNode.prototype.connect;
  AudioNode.prototype.connect = function (dst, ...a) {
    const r = conn.call(this, dst, ...a);
    if (this instanceof DynamicsCompressorNode && dst === this.context.destination && !window.__tap) {
      const sp = this.context.createScriptProcessor(4096, 2, 2);
      sp.onaudioprocess = (e) => { if (!window.__recOn) return; window.__rec.push(new Float32Array(e.inputBuffer.getChannelData(0)), new Float32Array(e.inputBuffer.getChannelData(1))); };
      conn.call(this, sp); conn.call(sp, this.context.destination); window.__tap = sp; window.__recOn = true;
      window.__tapT = this.context.currentTime;
    }
    return r;
  };` });
await c.send('Page.navigate', { url: 'file://' + html });
await new Promise((r) => setTimeout(r, +secs * 1000));
const b64 = await c.evaluate(`(() => { const r = window.__rec; const n = r.length / 2 * 4096; const y = new Float32Array(n * 2);
  for (let b = 0; b < r.length / 2; b++) { const l = r[b * 2], rr = r[b * 2 + 1]; for (let i = 0; i < 4096; i++) { y[(b * 4096 + i) * 2] = l[i]; y[(b * 4096 + i) * 2 + 1] = rr[i]; } }
  const u = new Uint8Array(y.buffer); let s = ''; for (let i = 0; i < u.length; i += 0x8000) s += String.fromCharCode.apply(null, u.subarray(i, i + 0x8000)); return btoa(s); })()`);
const buf = Buffer.from(b64, 'base64'); writeFileSync(out, buf);
console.log('recorded', (buf.length / 8 / 48000).toFixed(2), 's');
c.close(); process.exit(0);
