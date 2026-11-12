// Composites a station cover poster (1280x720) as an anime fighting-game key visual and renders it
// with Playwright + the system Chrome. Colour character busts (assets/portraits) are the heroes;
// the full-body battle-pose cutouts (assets/poster) are the dynamic action layer behind them.
//
//   node tools/make-poster.mjs            # writes assets/cover.png
//
import { readFileSync, writeFileSync } from 'node:fs';
import { join, resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

import { createRequire } from 'node:module';
const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(join(ROOT, 'third_party', 'wasmcart', 'package.json'));
const { chromium } = require('playwright');

const b64 = (p) => 'data:image/png;base64,' + readFileSync(p).toString('base64');
const bust = (k) => b64(join(ROOT, 'assets', 'portraits', `${k}.png`));
const sil = (k) => b64(join(ROOT, 'assets', 'poster', `${k}.png`));

// the action layer: colour battle-pose cutouts (shared KEYHOLDER bodies) behind the heroes
// (x/y are %, w is px, flip, rot, op, dim = brightness, blur px)
const SIL = [
  { k: 'ruth',    x: 29, y: 16, w: 250, flip: 0, rot: -4, op: 0.72, dim: 0.66, blur: 1.2 },
  { k: 'cal',     x: 74, y: 14, w: 270, flip: 1, rot: 5,  op: 0.72, dim: 0.66, blur: 1.2 },
  { k: 'dex',     x: 9,  y: 34, w: 250, flip: 0, rot: 3,  op: 0.6,  dim: 0.5,  blur: 2 },
  { k: 'rennick', x: 91, y: 38, w: 250, flip: 1, rot: -3, op: 0.6,  dim: 0.5,  blur: 2 },
  { k: 'champ',   x: 50, y: 22, w: 230, flip: 0, rot: 0,  op: 0.42, dim: 0.42, blur: 2.6 },
];

const html = `<!doctype html><html><head><meta charset="utf-8">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Anton&family=Archivo+Black&family=Barlow+Condensed:wght@600;700&display=swap" rel="stylesheet">
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  html,body { width:1280px; height:720px; overflow:hidden; }
  #stage { position:relative; width:1280px; height:720px; background:#14110d; overflow:hidden;
           font-family:'Barlow Condensed',sans-serif; }
  /* diagonal energy split: moheeb gold lower-left vs luis steel-blue upper-right */
  .bg { position:absolute; inset:0; }
  .bg.split { background:
     linear-gradient(118deg, #3a2a0a 0%, #241a0c 38%, #1a1f2b 62%, #16283a 100%); }
  .bg.vignette { background:radial-gradient(130% 90% at 50% 42%, transparent 40%, rgba(0,0,0,.72) 100%); }
  .bg.rays { background:repeating-conic-gradient(from 0deg at 50% 46%,
     rgba(255,255,255,.045) 0deg 2deg, transparent 2deg 9deg); mix-blend-mode:screen; opacity:.8; }
  .bg.grid { background-image:linear-gradient(rgba(255,255,255,.04) 1px, transparent 1px),
     linear-gradient(90deg, rgba(255,255,255,.04) 1px, transparent 1px); background-size:44px 44px;
     mask-image:radial-gradient(70% 60% at 50% 45%, #000 30%, transparent 85%); opacity:.5; }
  /* hazard stripe band behind the title */
  .hazard { position:absolute; left:-6%; right:-6%; top:60px; height:118px; transform:rotate(-2.4deg);
     background:repeating-linear-gradient(45deg, #f2ab1e 0 26px, #14110d 26px 52px); opacity:.16; }
  .sil { position:absolute; transform-origin:center bottom; }
  .clash { position:absolute; left:50%; top:52%; width:360px; height:360px; transform:translate(-50%,-50%);
     background:radial-gradient(circle, rgba(255,241,210,.9) 0%, rgba(242,171,30,.5) 18%, transparent 55%);
     mix-blend-mode:screen; opacity:.7; }
  /* the two leads */
  .hero { position:absolute; bottom:-14px; width:482px; }
  .hero .ring { position:absolute; left:50%; bottom:40px; width:360px; height:360px; transform:translateX(-50%);
     border-radius:50%; filter:blur(14px); }
  .hero img { position:relative; width:100%; display:block;
     filter:drop-shadow(0 8px 16px rgba(0,0,0,.6)) drop-shadow(0 0 2px rgba(0,0,0,.9)); }
  .hero.left  { left:-56px; transform:scaleX(1.02); }
  .hero.right { right:-56px; }
  .hero.right img { transform:scaleX(-1); }
  .hero.left .ring  { background:radial-gradient(circle, rgba(242,171,30,.85), transparent 68%); }
  .hero.right .ring { background:radial-gradient(circle, rgba(94,147,196,.85), transparent 68%); }
  .tag { position:absolute; bottom:150px; font-family:'Archivo Black',sans-serif; font-size:40px;
     letter-spacing:1px; color:#fff; text-shadow:0 3px 0 rgba(0,0,0,.55); }
  .tag small { display:block; font-family:'Barlow Condensed',sans-serif; font-weight:700; font-size:19px;
     letter-spacing:3px; margin-top:-2px; }
  .tag.left  { left:40px; color:#ffd36b; }
  .tag.right { right:40px; text-align:right; color:#a8cdf0; }
  .tag.left small, .tag.right small { color:#d7cfc3; }
  /* title */
  .title { position:absolute; top:52px; left:0; right:0; text-align:center; }
  .title h1 { font-family:'Anton',sans-serif; font-size:150px; line-height:.86; letter-spacing:1px;
     color:#f6c652; -webkit-text-stroke:3px #1a1106; transform:skewX(-7deg);
     text-shadow:0 5px 0 #7a4a12, 0 9px 0 #3a230a, 0 14px 22px rgba(0,0,0,.6); }
  .title h1 .amp { color:#fff; -webkit-text-stroke:3px #1a1106; }
  .title h1 .kill { color:#e4572e; -webkit-text-stroke:3px #140803; }
  .title .sub { margin-top:6px; font-family:'Barlow Condensed',sans-serif; font-weight:700;
     font-size:27px; letter-spacing:11px; color:#efe7da; text-indent:11px;
     text-shadow:0 2px 3px rgba(0,0,0,.8); }
  .edge { position:absolute; inset:0; box-shadow:inset 0 0 0 4px rgba(0,0,0,.55), inset 0 0 140px rgba(0,0,0,.55); }
</style></head>
<body><div id="stage">
  <div class="bg split"></div>
  <div class="bg grid"></div>
  <div class="bg rays"></div>
  <div class="hazard"></div>
  ${SIL.map(s => `<img class="sil" src="${sil(s.k)}" style="left:${s.x}%; top:${s.y}%; width:${s.w}px; opacity:${s.op}; filter:brightness(${s.dim}) saturate(.85) blur(${s.blur}px) drop-shadow(0 0 2px rgba(0,0,0,.6)); transform:translate(-50%,-10%) rotate(${s.rot}deg) scaleX(${s.flip ? -1 : 1});">`).join('\n  ')}
  <div class="bg vignette"></div>
  <div class="clash"></div>

  <div class="hero left"><div class="ring"></div><img src="${bust('moheeb')}"></div>
  <div class="hero right"><div class="ring"></div><img src="${bust('luis')}"></div>

  <div class="title">
    <h1>COFFEE <span class="amp">&amp;</span> <span class="kill">KILL</span></h1>
    <div class="sub">HACKERSPACE BRAWLER</div>
  </div>

  <div class="tag left">MOHEEB<small>DO-OCRAT</small></div>
  <div class="tag right">LUIS<small>THE FABRICATOR</small></div>
  <div class="edge"></div>
</div></body></html>`;

writeFileSync('/tmp/ck-poster.html', html);
const browser = await chromium.launch({ executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 }, deviceScaleFactor: 1 });
await page.goto(pathToFileURL('/tmp/ck-poster.html').href, { waitUntil: 'networkidle' });
await page.evaluate(() => document.fonts.ready);
await page.waitForTimeout(400);
const out = join(ROOT, 'assets', 'cover.png');
await page.locator('#stage').screenshot({ path: out });
await browser.close();
console.log('wrote', out);
