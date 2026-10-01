#!/usr/bin/env python3
"""Fails when the checked-in OLED assets drift from their sources.

1. Regenerates RailyPinsP1/oled_assets.h and oled_golden/ into a temp dir
   with tools/gen_oled_assets.py: any byte of the header, any pixel of a
   golden frame, or the manifest differing fails (commit the regenerated
   files).
2. Parses the site's mascot SVG
   (frontend-visualization/src/components/agent/agent-mascot.tsx) and fails
   when a shape's path, rect, circle, stroke or eye position differs from
   the generator's copy in tools/oled_assets/shapes.py: the keyring must
   draw the same six shapes as the site (spec §16).
3. Checks the asset budget (24 KB since 0.2.16-qa; 16 KB before, implementation plan §2).
4. Compares the fall (scene `fell`, pebble, satin, ru) with the approved
   design, docs/pins/keyring-oled/concept/raw/k23-lost-f1..f8.png, bit for
   bit outside the battery icon (x 52-63, y 0-6): the firmware's status row
   is the link icon only until the battery characteristic ships. The
   agent's counts (found, found_self, nobody, looking) likewise against
   k05-found, a02-found, k06-nothing and a03-looking.

The site and design checks need the main repository; the public raily-pin
mirror has no frontend or docs and skips them, saying so.
"""
import pathlib
import re
import subprocess
import sys
import tempfile

from PIL import Image

TESTS = pathlib.Path(__file__).resolve().parent
FIRMWARE = TESTS.parent
TOOLS = FIRMWARE / "tools"
HEADER = FIRMWARE / "RailyPinsP1" / "oled_assets.h"
GOLDEN = TESTS / "oled_golden"
TSX = FIRMWARE.parents[1] / "frontend-visualization" / "src" / "components" / "agent" / "agent-mascot.tsx"
CONCEPT_RAW = FIRMWARE.parents[1] / "docs" / "pins" / "keyring-oled" / "concept" / "raw"
BATTERY_BOX = (52, 0, 63, 6)   # x0, y0, x1, y1 inclusive: the pack's battery icon, not drawn by the pin yet

sys.dont_write_bytecode = True
sys.path.insert(0, str(TOOLS))
import gen_oled_assets as G  # noqa: E402
from oled_assets import shapes as S  # noqa: E402


def check_generated(failures):
    with tempfile.TemporaryDirectory() as tmp:
        header = pathlib.Path(tmp) / "oled_assets.h"
        golden = pathlib.Path(tmp) / "golden"
        run = subprocess.run([sys.executable, "-B", str(TOOLS / "gen_oled_assets.py"), "--header", str(header),
                              "--golden", str(golden)], capture_output=True, text=True)
        if run.returncode != 0:
            failures.append("gen_oled_assets.py failed: " + (run.stderr or run.stdout).strip())
            return
        print(run.stdout.strip())
        if run.stderr.strip():
            print(run.stderr.strip())
        if header.read_bytes() != HEADER.read_bytes():
            failures.append("RailyPinsP1/oled_assets.h is stale: run tools/gen_oled_assets.py and commit it")
        fresh = {p.name for p in golden.iterdir()}
        kept = {p.name for p in GOLDEN.iterdir()}
        for name in sorted(fresh ^ kept):
            failures.append(f"oled_golden/{name}: {'missing' if name in fresh else 'not generated any more'}")
        if (golden / "manifest.txt").read_bytes() != (GOLDEN / "manifest.txt").read_bytes():
            failures.append("oled_golden/manifest.txt is stale")
        for name in sorted(fresh & kept):
            if not name.endswith(".png"):
                continue
            a, b = Image.open(golden / name).convert("1"), Image.open(GOLDEN / name).convert("1")
            if a.size != b.size or a.tobytes() != b.tobytes():
                failures.append(f"oled_golden/{name}: pixels differ from the generator")
    if failures:
        import PIL
        if PIL.__version__ != G.PILLOW_REFERENCE:
            failures.append(f"(Pillow {PIL.__version__} here, the assets were made with {G.PILLOW_REFERENCE})")


def _attrs(tag):
    return dict(re.findall(r'(\w+)="([^"]*)"', tag))


def check_site(failures):
    if not TSX.exists():
        print(f"SKIP site shapes: {TSX.name} not in this checkout (public mirror)")
        return
    src = TSX.read_text()
    blocks = dict(re.findall(r'look\.shape === "(\w+)" && \(\s*(<[^>]*?/>)', src, re.S))
    if set(blocks) != set(S.SHAPES):
        failures.append(f"site shapes {sorted(blocks)} != generator shapes {sorted(S.SHAPES)}")
        return
    for shape, tag in sorted(blocks.items()):
        a = _attrs(tag)
        if shape in S.PATHS:
            if a.get("d") != S.PATHS[shape]:
                failures.append(f"{shape}: site path {a.get('d')!r} != generator {S.PATHS[shape]!r}")
        if shape in S.RECTS:
            x, y, w, h, rx, rot = S.RECTS[shape]
            site = tuple(float(a.get(k, "nan")) for k in ("x", "y", "width", "height", "rx"))
            m = re.fullmatch(r"rotate\((\S+) 32 32\)", a.get("transform", "rotate(0 32 32)"))
            site_rot = float(m.group(1)) if m else float("nan")
            if site != (x, y, w, h, rx) or site_rot != rot:
                failures.append(f"{shape}: site rect {site} rotate {site_rot} != generator {S.RECTS[shape]}")
        if shape == "loop":
            site = tuple(float(a.get(k, "nan")) for k in ("cx", "cy", "r"))
            stroke = re.search(r"strokeWidth=\"(\d+)\"", tag)
            if site + (float(stroke.group(1)) if stroke else float("nan"),) != tuple(float(v) for v in S.LOOP):
                failures.append(f"loop: site circle {site} stroke {stroke and stroke.group(1)} != generator {S.LOOP}")
        if shape == "fold":
            stroke = re.search(r"strokeWidth=\"(\d+)\"", tag)
            if not stroke or int(stroke.group(1)) != S.FOLD_STROKE:
                failures.append(f"fold: site stroke {stroke and stroke.group(1)} != generator {S.FOLD_STROKE}")
    eyes = re.findall(r'<ellipse cx="(\d+)" cy=\{hollow \? (\d+) : (\d+)\}', src)
    want = [(str(x), str(S.EYES_Y_HOLLOW), str(S.EYES_Y)) for x in S.EYES_X]
    if eyes != want:
        failures.append(f"site eyes {eyes} != generator {want}")
    if not [f for f in failures if f.startswith(S.SHAPES) or f.startswith("site ")]:
        print(f"PASS site mascot shapes match the generator ({', '.join(S.SHAPES)})")


def check_fall_design(failures):
    if not CONCEPT_RAW.exists():
        print(f"SKIP fall design: {CONCEPT_RAW.name}/ not in this checkout (public mirror)")
        return
    x0, y0, x1, y1 = BATTERY_BOX
    before = len(failures)
    for i in range(1, 9):
        mine = GOLDEN / f"fell-f{i}-pebble-satin-ru.png"
        pack = CONCEPT_RAW / f"k23-lost-f{i}.png"
        if not mine.exists() or not pack.exists():
            failures.append(f"fall frame {i}: {mine.name if not mine.exists() else pack.name} is missing")
            continue
        a, b = Image.open(mine).convert("1"), Image.open(pack).convert("1")
        if a.size != b.size:
            failures.append(f"fall frame {i}: {a.size} vs the pack's {b.size}")
            continue
        pa, pb = a.load(), b.load()
        diff = sum(1 for y in range(a.size[1]) for x in range(a.size[0])
                   if bool(pa[x, y]) != bool(pb[x, y]) and not (x0 <= x <= x1 and y0 <= y <= y1))
        if diff:
            failures.append(f"fall frame {i}: {diff} pixels differ from concept/raw/{pack.name}")
    if len(failures) == before:
        print("PASS the fall matches the approved design (k23-lost f1-f8) bit for bit outside the battery icon")


# The agent's counts, ru, pebble/satin, as the pack drew them (k05 with
# «3», a03 with «+5»): golden stem -> concept/raw frame.
COUNT_DESIGN = [(f"found-f{i}-pebble-satin-ru-3", f"k05-found-f{i}") for i in range(1, 5)] + [
    (f"found_self-f{i}-pebble-satin-ru", f"a02-found-f{i}") for i in (1, 2)] + [
    (f"nobody-f{i}-pebble-satin-ru", f"k06-nothing-f{i}") for i in (1, 2)] + [
    ("looking-f1-pebble-satin-ru-5", "a03-looking-f1")] + [
    ("request-f1-pebble-satin-ru", "a06-request-f1"), ("accepted-f1-pebble-satin-ru", "a07-accepted-f1"),
    ("talks-f1-pebble-satin-ru", "w02-negotiation-f1"), ("door-f1-pebble-satin-ru", "w01-door-f1"),
    ("question-f1-pebble-satin-ru", "a04-question-f1"), ("question-f2-pebble-satin-ru", "a04-question-f2"),
    ("report-f1-pebble-satin-ru", "a05-report-f1"), ("report-f2-pebble-satin-ru", "a05-report-f2")]
# The report's word is the spec's «отчёт»: the pack's «отчёт готов» is 66 px,
# over the 60 px row, so the report is compared above the word row only.
WORD_ROW_Y = 98
ABOVE_WORD_ROW = {"report-f1-pebble-satin-ru", "report-f2-pebble-satin-ru"}


def check_count_design(failures):
    if not CONCEPT_RAW.exists():
        print(f"SKIP count design: {CONCEPT_RAW.name}/ not in this checkout (public mirror)")
        return
    x0, y0, x1, y1 = BATTERY_BOX
    before = len(failures)
    for stem, raw in COUNT_DESIGN:
        mine, pack = GOLDEN / f"{stem}.png", CONCEPT_RAW / f"{raw}.png"
        if not mine.exists() or not pack.exists():
            failures.append(f"count design: {mine.name if not mine.exists() else pack.name} is missing")
            continue
        a, b = Image.open(mine).convert("1"), Image.open(pack).convert("1")
        if a.size != b.size:
            failures.append(f"count design: {stem} is {a.size}, the pack's {pack.name} is {b.size}")
            continue
        pa, pb = a.load(), b.load()
        rows = WORD_ROW_Y if stem in ABOVE_WORD_ROW else a.size[1]
        diff = sum(1 for y in range(rows) for x in range(a.size[0])
                   if bool(pa[x, y]) != bool(pb[x, y]) and not (x0 <= x <= x1 and y0 <= y <= y1))
        if diff:
            failures.append(f"count design: {stem} differs from concept/raw/{pack.name} ({diff} pixels)")
    if len(failures) == before:
        print("PASS the counts and notifications match the approved design (k05, a02, k06, a03, a04-a07, w01, w02) "
              "bit for bit outside the battery icon (the report above its word row)")


def check_timing(failures):
    errors = G.timing_errors(G.build())
    failures.extend(errors)
    if not errors:
        print("PASS every scene frame lasts 1-65535 ms, no looping scene lasts 0 ms")


def check_budget(failures):
    size = G.asset_bytes(G.build())
    if size > G.ASSET_BUDGET:
        failures.append(f"assets are {size} B, over the {G.ASSET_BUDGET} B budget")
    else:
        print(f"PASS assets {size} B within the {G.ASSET_BUDGET} B budget")


def main():
    failures = []
    check_generated(failures)
    if not failures:
        print("PASS oled_assets.h and oled_golden/ match the generator byte for byte")
    check_site(failures)
    check_fall_design(failures)
    check_count_design(failures)
    check_timing(failures)
    check_budget(failures)
    for failure in failures:
        print("FAIL", failure)
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
