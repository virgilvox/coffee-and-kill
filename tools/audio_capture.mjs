// Records what the real page posts to its Bellows kernel (every message on
// the AudioWorkletNode's port, with AudioContext.currentTime) for N seconds of
// the title screen, so tools/audio_bake.mjs's stand-in for Gr can be checked
// against it.
//   node tools/audio_capture.mjs <coffee-and-kill.html> <out.json> [seconds]
import { writeFileSync } from 'node:fs';
import { chrome } from './audio_cdp.mjs';

const [html, out, secs = '12'] = process.argv.slice(2);
const c = await chrome({ port: 9335 });
await c.send('Page.addScriptToEvaluateOnNewDocument', { source: `
  window.__msgs = []; window.__ctx = null;
  const AC = window.AudioContext;
  window.AudioContext = class extends AC { constructor(o) { super(o); window.__ctx = this; } };
  const post = MessagePort.prototype.postMessage;
  MessagePort.prototype.postMessage = function (m, t) {
    try { if (m && m.type) window.__msgs.push({ t: window.__ctx ? window.__ctx.currentTime : -1, m: JSON.parse(JSON.stringify(m)) }); } catch (e) {}
    return post.call(this, m, t);
  };` });
await c.send('Page.navigate', { url: 'file://' + html });
await new Promise((r) => setTimeout(r, +secs * 1000));
const data = await c.evaluate(`({ rate: window.__ctx && window.__ctx.sampleRate, state: window.__ctx && window.__ctx.state, now: window.__ctx && window.__ctx.currentTime, msgs: window.__msgs })`);
writeFileSync(out, JSON.stringify(data));
console.log('rate', data.rate, 'state', data.state, 'now', data.now, 'messages', data.msgs.length);
c.close();
process.exit(0);
