#!/usr/bin/env bash
# Regenerates assets/music from the original page (needs node, Chrome, ffmpeg):
#   tools/audio_rebuild.sh <coffee-and-kill.html>
#   1. build/audio-src: the page's bundle, pretty printed (esbuild), and its game section
#   2. tools/audio_bake.mjs: the fight loops and dance songs, from the page's own Bellows kernel
#   3. tools/audio_sfx.mjs: every SFX variant, from Chrome's WebAudio running the page's SA code
#   4. FLAC, 16 bit (the SFX at half level: some peak above 1)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"; cd "$ROOT"
HTML="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
ESBUILD="${ESBUILD:-$(command -v esbuild || echo "$HOME/Projects/flowgram/node_modules/.bin/esbuild")}"
mkdir -p build/audio-src build/music-raw assets/music
# the bundle, its base64 blobs and long number runs stubbed so esbuild keeps the line layout the tools index
python3 - "$HTML" <<'PY'
import re, sys
t = open(sys.argv[1], encoding='utf-8').read()
s = t.index('<script>', t.index('</style>')); e = t.index('</script>', s)
js = t[s + 8:e]
js = re.sub(r'([A-Za-z0-9+/]{400,}={0,2})', lambda m: 'B64_%d' % len(m.group(1)), js)
js = re.sub(r'(-?\d*\.?\d+(e-?\d+)?,){200,}-?\d*\.?\d+', lambda m: '0/*NUMS_%d*/' % len(m.group(0)), js)
open('build/audio-src/stripped.js', 'w').write(js)
PY
"$ESBUILD" build/audio-src/stripped.js --log-level=warning > build/audio-src/pretty.js
# game.js: the pretty bundle from the game's first module (line 14122) on, as the tools index it
python3 - <<'PY'
L = open('build/audio-src/pretty.js').read().split('\n')
open('build/audio-src/game.js', 'w').write('\n'.join(L[14121:15212] + ['//==== AUDIO+UI'] + L[20884:21540] + ['//==== GAME'] + L[22726:24625] + ['//==== NET'] + L[26149:26359] + ['//==== APP'] + L[27016:27361]) + '\n')
PY
node tools/audio_bake.mjs "$HTML" build/music-raw
node tools/audio_sfx.mjs build/music-raw
node tools/audio_sfxpack.mjs build/music-raw/sfx.bin build/music-raw/sfx_all.f32 assets/music/sfx_index.bin
for f in fight0 fight1 fight2 dance_funk dance_disco dance_rave dance_surf dance_lounge; do
  ffmpeg -v error -y -f f32le -ar 48000 -ac 2 -i "build/music-raw/$f.f32" -c:a flac -sample_fmt s16 -compression_level 12 "assets/music/$f.flac"
done
ffmpeg -v error -y -f f32le -ar 48000 -ac 1 -i build/music-raw/sfx_all.f32 -af volume=0.5 -c:a flac -sample_fmt s16 -compression_level 12 assets/music/sfx.flac
du -sh assets/music
