#!/usr/bin/env python3
"""Fails unless the C++ composer drew every golden frame bit for bit.

test_oled_compose writes one raw 1 KB canvas per line of
oled_golden/manifest.txt (64 x 128, row-major, most significant bit left);
this compares each with its PNG, which tools/gen_oled_assets.py drew with
its own Python reference from the same tables.

    check_oled_golden.py <dir with the .bin canvases>
"""
import pathlib
import sys

from PIL import Image

GOLDEN = pathlib.Path(__file__).resolve().parent / "oled_golden"
W, H = 64, 128


def main():
    out = pathlib.Path(sys.argv[1])
    failures, checked = [], 0
    for line in (GOLDEN / "manifest.txt").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name = line.split()[0]
        if not (GOLDEN / name).is_file():
            failures.append(f"{name}: in the manifest but not in oled_golden/")
            continue
        png = Image.open(GOLDEN / name).convert("1")
        raw = out / (name + ".bin")
        if png.size != (W, H):
            failures.append(f"{name}: golden is {png.size}, not {W} x {H}")
            continue
        if not raw.exists():
            failures.append(f"{name}: the composer wrote nothing")
            continue
        data = raw.read_bytes()
        if len(data) != W * H // 8:
            failures.append(f"{name}: the composed canvas is {len(data)} B, not {W * H // 8} B")
            continue
        px = png.load()
        diff = 0
        for y in range(H):
            for x in range(W):
                bit = (data[y * (W // 8) + x // 8] >> (7 - x % 8)) & 1
                diff += bit != (1 if px[x, y] else 0)
        checked += 1
        if diff:
            failures.append(f"{name}: {diff} pixels differ")
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print(f"PASS {checked} composed frames match oled_golden/ bit for bit")
    sys.exit(1 if failures or not checked else 0)


if __name__ == "__main__":
    main()
