#!/usr/bin/env bash
# Builds the audio test cart (tools/test_audio.c) into build-audio and packs
# the music and SFX assets into it (build.sh does not copy assets/music yet).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
OUT="$ROOT/build-audio" CK_SRCS="src/ck.c src/main.c src/input.c src/overlay.c src/fx.c src/stubs.c src/sfx.c src/songs.c tools/test_audio.c" ./build.sh >/dev/null
cp assets/music/* build-audio/assets/
node third_party/wasmcart/bin/wasmcart-pack.js --wasm build-audio/coffee-and-kill.wasm --assets build-audio/assets \
  --name "COFFEE & KILL" --version 0.1.0 --width 1280 --height 720 --players 2 --output build-audio/coffee-and-kill.wasc >/dev/null
echo "built build-audio/coffee-and-kill.wasc ($(wc -c < build-audio/coffee-and-kill.wasc | tr -d ' ') bytes)"
