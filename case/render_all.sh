#!/bin/bash
# Renders the preview images from build/stage2 (run `python keyring_case.py stage2` first).
#   ./render_all.sh                      # every scheme, iso + front
#   ./render_all.sh white_deep black_pale   # just these schemes
#   VERSION=v2 ./render_all.sh v1 v5     # case v2 (stage2 v2 first): build/stage2-v2 -> preview/render-v2-<scheme>-*.png
# BLENDER, PY and SAMPLES can be overridden; one Blender job at a time.
set -e
cd "$(dirname "$0")"
BLENDER=${BLENDER:-/Applications/Blender.app/Contents/MacOS/Blender}
PY=${PY:-../../../.venv/bin/python}
export DYLD_FALLBACK_LIBRARY_PATH=${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}
SAMPLES=${SAMPLES:-128}
SCHEMES=${*:-white_deep white_pale black_deep black_pale graphite_g1 graphite_g2}
VERSION=${VERSION:-v1}
case "$VERSION" in v1|v2|v2flat) ;; *) echo "unknown VERSION $VERSION (v1, v2, v2flat)" >&2; exit 1 ;; esac
if [ "$VERSION" = v1 ]; then SRC=build/stage2; TAG=; else SRC="build/stage2-$VERSION"; TAG="$VERSION-"; fi
[ -d "$SRC" ] || { echo "$SRC missing: run python keyring_case.py stage2 $VERSION first" >&2; exit 1; }
for s in $SCHEMES; do
  for cam in iso front; do
    out="preview/render-$TAG$s-$cam.png"
    job() { "$BLENDER" -b -P blender_render.py -- "$SRC" "$PWD/$out" "$cam" "$s" "$SAMPLES" > /dev/null 2>&1; }
    job || job          # Blender 5.2 on Metal occasionally aborts at start-up; one retry
    "$PY" keyring_case.py flatten "$out" "#EDEEF1"      # studio: the white ads' paper; gallery renders are opaque
    echo "$out"
  done
done
