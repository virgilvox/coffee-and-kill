#!/usr/bin/env bash
# Adds the shader states a test run printed (lines like "lambert+map+skin+fog2+atest+ds+l1b01",
# after a "[cart]" line) to the program table and rebuilds.
#   tools/regen.sh <run log> [OUT dir to rebuild as well]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build"
sed 's/^\[cart\] //' "$1" | grep -E '^(standard|basic|lambert|phong|physical|toon|points|line|sprite|shader|depth|distance|shadow)[a-z0-9+]*$' | sort -u > "$ROOT/build/missing.txt" || true
echo "regen: $(wc -l < "$ROOT/build/missing.txt" | tr -d ' ') states"
(cd "$ROOT" && REGEN_STATES=1 OUT="$ROOT/build" ./build.sh)
[ -n "${2:-}" ] && (cd "$ROOT" && OUT="$2" EXTRA_CFLAGS="${EXTRA_CFLAGS:-}" ./build.sh) || true
