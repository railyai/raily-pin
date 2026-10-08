#!/bin/bash
# Checkpoint A (the frozen case for the Grove vibration module): regenerates and slices its three plates from this
# commit, then gates them. Tray 17.1 (the #17 v1.3 'fit' plate, printed as P17a with the old 17.2 shelf beside it:
# only its tray belongs to A), shelf 17.2b, cover 18.2-G (face + frame + 13 round dowels + the ТЕСТ joint).
#   bash bambu/checkpoint_a.sh            (from hardware/raily-pin-public/case)
set -euo pipefail
cd "$(dirname "$0")"
PY=${PY:-../../../../.venv/bin/python}
KC_VERSION=v1.3 $PY build_bambu.py --proto-number 17 parts back
KC_VERSION=v1.3 $PY build_bambu.py --proto-number 17 fit
bash run-fit.sh
KC_VERSION=v1.3 $PY build_bambu.py --proto-number 17 shelf2b
bash run-shelf2b-v1.3.sh
KC_VERSION=v1.3-p18.2-G $PY build_bambu.py cover2
bash run-cover2-v1.3-p18.2-G.sh
T=out-v1.3/a1m-fit/a1m-fit-sliced.3mf     # the v1.3 fit job's slice (out/a1m-fit is an older v1 one)
for f in "$T" out-v1.3/a1m-shelf2b-v1.3/a1m-shelf2b-v1.3-sliced.3mf \
         out-v1.3-p18.2-G/a1m-cover2-v1.3-p18.2-G/a1m-cover2-v1.3-p18.2-G-sliced.3mf; do
  echo "== $f"; $PY gcode_gate.py "$f" | grep -v 'short bridge' || true   # 17.1: 2 one-cell specks, as the printed P17a
done
$PY fit_gate.py out-v1.3-p18.2-G/a1m-cover2-v1.3-p18.2-G/a1m-cover2-v1.3-p18.2-G-sliced.3mf \
    --ref "${REF_18_1C:-$HOME/Desktop/Raily keyring 3D/P18-1c-cover-A1mini.gcode.3mf}"
