#!/usr/bin/env bash
# Builds COFFEE & KILL as a wasmcart GL cart (build/coffee-and-kill.wasc).
#
#   ./build.sh                 # full build
#   REGEN_STATES=1 ./build.sh  # regenerate src/gen/ck_gl.c from tools/states.txt
#                              # (merges build/missing.txt, the states a run could not draw)
#   EXTRA_CFLAGS=... ./build.sh
#   OUT=build-ui CK_SRCS="src/a.c src/b.c" ./build.sh   # another tree, an explicit source list (a test driver)
#
# Needs: emcc (emsdk), node >= 22, third_party/{three.c,wasmcart,bellows,canvas_ity}.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
THREE="$ROOT/third_party/three.c"
WASMCART="$ROOT/third_party/wasmcart"
OUT="${OUT:-$ROOT/build}"   # OUT=build-x: a separate build tree (parallel work)
TOBJ="$OUT/obj/three_core"
GOBJ="$OUT/obj/game"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
NAME=coffee-and-kill

if ! command -v emcc >/dev/null; then
  [ -f "$HOME/emsdk/emsdk_env.sh" ] && source "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
command -v emcc >/dev/null || { echo "emcc not found: install emsdk (https://emscripten.org)" >&2; exit 1; }
[ -d "$WASMCART/node_modules" ] || (cd "$WASMCART" && npm install --silent)

mkdir -p "$TOBJ" "$GOBJ" "$ROOT/src/gen"
OPT="-O2 -DNDEBUG -msimd128 -msse2"

# three.c, built once and cached (the core GL program table; the game's own table is src/gen/ck_gl.c)
pids=()
for s in math core geometry curves animation raycaster loaders gltf renderer backend_gles gen_program gen/programs_core_gl gen/programs_dfg; do
  o="$TOBJ/${s//\//_}.o"
  if [ ! -f "$o" ] || [ "$THREE/src/$s.c" -nt "$o" ]; then
    emcc -c "$THREE/src/$s.c" -o "$o" $OPT -std=c99 -DT3_WASMCART -DT3_TABLE_CORE \
      -I "$WASMCART/include" -I "$THREE/include" -I "$THREE/src" &
    pids+=($!)
    while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do sleep 0.05; done
  fi
done
fail=0; for p in "${pids[@]+"${pids[@]}"}"; do wait "$p" || fail=1; done
[ $fail = 0 ] || { echo "three.c compile failed" >&2; exit 1; }

# the r186 programs this game draws (tools/states.txt) as a C table; committed, so a normal
# build needs no three.js. REGEN_STATES=1 merges build/missing.txt and captures what is new.
if [ "${REGEN_STATES:-0}" = 1 ] || [ ! -f "$ROOT/src/gen/ck_gl.c" ]; then
  touch "$ROOT/tools/states.txt"
  miss=(); [ -s "$OUT/missing.txt" ] && miss=(--missing "$OUT/missing.txt")
  gen() { (cd "$ROOT" && node "$THREE/tools/gen-project-table.mjs" --name ck --out src/gen --states tools/states.txt \
     --cache build/t3-captures "$@"); }
  gen "${miss[@]+"${miss[@]}"}"      # captures anything new from three.js
  # KEYHOLDER's bodies are cel lit: their lambert+skin programs get two bands (tools/cel-patch.mjs)
  node "$ROOT/tools/cel-patch.mjs" "$ROOT/build/t3-captures"
  gen                                # emits the table with the patched text
fi

GAMEF="-DWC_USE_GL -DWC_USE_NET_PEER ${EXTRA_CFLAGS:-}"

# C++ parts: the canvas (canvas_ity) and the Bellows music engine; cached like three.c
cxx() {   # src obj extra-flags...
  local src="$1" obj="$2"; shift 2
  if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$ROOT/src/$(basename "${src%.*}").h" -nt "$obj" ]; then
    em++ -c "$src" -o "$obj" $OPT -std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off $GAMEF \
      -I "$ROOT/src" -I "$ROOT/third_party" -I "$WASMCART/include" "$@"
  fi
}
cxx "$ROOT/src/cv.cpp" "$GOBJ/cv.o" -I "$ROOT/third_party/canvas_ity"
cxx "${MUSIC_CPP:-$ROOT/src/music.cpp}" "$GOBJ/music.o" -DBELLOWS_FAST_MATH=1 -I "$ROOT/third_party/bellows"

emcc $OPT -std=gnu11 -Wall -Wshadow -Wno-unused-function -ffp-contract=off -DT3_WASMCART $GAMEF \
  -I "$WASMCART/include" -I "$THREE/include" -I "$THREE/src" -I "$ROOT/src" -I "$ROOT/third_party" \
  ${CK_SRCS:-"$ROOT"/src/*.c "$ROOT"/src/kh/*.c} "$ROOT/src/gen/ck_gl.c" "$GOBJ/cv.o" "$GOBJ/music.o" "$TOBJ"/*.o \
  -sSTANDALONE_WASM=1 -sEXPORTED_FUNCTIONS='["_wc_init","_wc_render","_wc_get_info","_wc_set_seed","_malloc","_free"]' \
  -sERROR_ON_UNDEFINED_SYMBOLS=0 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 \
  --no-entry -o "$OUT/$NAME.wasm"

# a cart may import only gl, wc_* and WASI
WASMCART="$WASMCART" node "$THREE/wasm/check-imports.mjs" "$OUT/$NAME.wasm"

rm -rf "$OUT/assets" && mkdir -p "$OUT/assets"
cp "$ROOT"/assets/fonts/*.ttf "$OUT/assets/"
cp "$ROOT"/assets/anim/*.bin "$OUT/assets/"
for m in $(cat "$ROOT/tools/models.txt"); do cp "$ROOT/assets/models/$m.glb" "$OUT/assets/"; done
# KEYHOLDER's bodies, stages and signs (tools/import-kh.mjs)
cp "$ROOT"/assets/kh/* "$OUT/assets/"
# the fighter select's portraits (tools/bake-portraits.mjs) and the stage select's stills (tools/bake-stills.mjs)
for f in "$ROOT"/assets/portraits/*.png; do [ -f "$f" ] && cp "$f" "$OUT/assets/portrait_$(basename "$f")"; done
for f in "$ROOT"/assets/stills/*.jpg; do [ -f "$f" ] && cp "$f" "$OUT/assets/still_$(basename "$f")"; done
# the music and SFX, rendered from the page's own audio code (tools/audio_rebuild.sh)
cp "$ROOT"/assets/music/* "$OUT/assets/"
# the fighters' spoken lines (tools/bake_voices.py): Moheeb and Luis are Kyutai clones, the rest Kokoro
[ -f "$ROOT/assets/voice/lines.flac" ] && cp "$ROOT"/assets/voice/lines.flac "$ROOT"/assets/voice/lines_index.bin "$OUT/assets/"

# manifest: the CLASP relay is the only host the cart may dial (online versus and phone pads)
node "$WASMCART/bin/wasmcart-pack.js" --wasm "$OUT/$NAME.wasm" --assets "$OUT/assets" \
  --name "COFFEE & KILL" --version 0.3.0 --width 1280 --height 720 --players 2 \
  --controls dpad,left_stick,a,b,x,y,l,r,left_trigger,right_trigger,start,select \
  --ws relay.clasp.to --output "$OUT/$NAME.wasc" >/dev/null
echo "built $OUT/$NAME.wasc ($(wc -c < "$OUT/$NAME.wasc" | tr -d ' ') bytes)"
# the default tree also makes the web version: the cart and the one-file page (shell, the
# wasmcart web host and the cart inlined: tools/inline-html.mjs)
if [ "$OUT" = "$ROOT/build" ]; then
  mkdir -p "$ROOT/web" && cp "$OUT/$NAME.wasc" "$ROOT/web/$NAME.wasc"
  node "$ROOT/tools/inline-html.mjs" "$ROOT/web/index.src.html" "$ROOT/web/vendor/host-bundle.js" "$OUT/$NAME.wasc" "$ROOT/web/$NAME.html"
fi
