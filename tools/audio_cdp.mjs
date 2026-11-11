// A minimal headless Chrome over the DevTools protocol (Node's own
// WebSocket, no packages), for the audio tools: open a page, evaluate.
import { spawn } from 'node:child_process';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';

export async function chrome({ port = 9333, args = [] } = {}) {
  const dir = mkdtempSync(join(tmpdir(), 'ck-audio-'));
  const proc = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${dir}`,
    '--autoplay-policy=no-user-gesture-required', '--no-first-run', '--no-default-browser-check',
    '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', ...args, 'about:blank'], { stdio: 'ignore' });
  let targets;
  for (let i = 0; i < 100; i++) {
    try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); if (targets.length) break; } catch {}
    await new Promise((r) => setTimeout(r, 100));
  }
  const page = targets.find((t) => t.type === 'page');
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((r) => ws.addEventListener('open', r, { once: true }));
  let id = 0; const pending = new Map(); const listeners = [];
  ws.addEventListener('message', (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { const { res, rej } = pending.get(m.id); pending.delete(m.id); m.error ? rej(new Error(m.error.message)) : res(m.result); }
    else for (const l of listeners) l(m);
  });
  const send = (method, params = {}) => new Promise((res, rej) => { const i = ++id; pending.set(i, { res, rej }); ws.send(JSON.stringify({ id: i, method, params })); });
  const evaluate = async (expression, { awaitPromise = true } = {}) => {
    const r = await send('Runtime.evaluate', { expression, awaitPromise, returnByValue: true });
    if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails).slice(0, 800));
    return r.result.value;
  };
  await send('Runtime.enable');
  await send('Page.enable');
  return { send, evaluate, on: (f) => listeners.push(f), close: () => { try { ws.close(); } catch {} proc.kill('SIGKILL'); } };
}
