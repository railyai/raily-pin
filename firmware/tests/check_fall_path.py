#!/usr/bin/env python3
"""Fails when the fall could vibrate, light the LED or touch the radio.

The owner decided on 2026-09-28 (docs/pins/keyring-oled.md §17): a dropped
pin lights the screen («Упал. Было больно») and does not vibrate. The pure
half is host-tested (tests/test_screen_fall.cpp); the sketch half is not
host-compiled, so this reads RailyPinsP1.ino:

- screenOnFall() only raises pendingScreenFall: no motor byte, no LED
  byte, no BLE write or notify, no press;
- pendingScreenFall is raised only there and drained only in
  updateScreen(), which hands it to screen.onFall() and nothing else,
  before screen.tick() (a press in the same pass wins);
- serial `of` (BENCH_FALL) runs screenOnFall(), the real path, never a
  static demo.
"""
import pathlib
import re
import sys

SKETCH = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1" / "RailyPinsP1.ino"
FORBIDDEN = re.compile(r"\b(pendingRhythmAck|pendingFeedback|pendingScreenPress\w*|rhythm\w*|updateMotor|motor\w*|"
                       r"digitalWrite|publishEventAck|emitButtonEvent|notify|write|Bluefruit|showDemo)\b")


def function_body(source, signature):
    start = source.find(signature)
    if start < 0:
        return None
    depth, i = 0, source.index("{", start)
    for j in range(i, len(source)):
        depth += {"{": 1, "}": -1}.get(source[j], 0)
        if depth == 0:
            return source[i:j + 1]
    return None


def case_body(source, label):
    start = source.find("case %s:" % label)
    if start < 0:
        return None
    end = source.find("break;", start)
    return source[start:end] if end > 0 else None


def main():
    failures = []
    ino = SKETCH.read_text()
    hook = function_body(ino, "static void screenOnFall() {")
    if hook is None:
        failures.append("screenOnFall() not found in RailyPinsP1.ino")
    else:
        code = re.sub(r"//[^\n]*", "", hook)
        for match in FORBIDDEN.finditer(code):
            failures.append(f"screenOnFall() uses {match.group(1)}: the fall is the screen only (no motor, LED, radio)")
        if "pendingScreenFall.store(true" not in code:
            failures.append("screenOnFall() does not raise pendingScreenFall")
    stores = re.findall(r"pendingScreenFall\.store\(", ino)
    if len(stores) != 1:
        failures.append(f"pendingScreenFall is raised {len(stores)} times: only screenOnFall() may raise it")
    screen = function_body(ino, "static void updateScreen(uint32_t now, uint8_t ack, bool linkUp) {") or ""
    drains = re.findall(r"pendingScreenFall\.exchange\(", ino)
    if len(drains) != 1 or "pendingScreenFall.exchange(" not in screen:
        failures.append("pendingScreenFall must be drained exactly once, in updateScreen()")
    uses = re.findall(r"\bfall\b", re.sub(r"//[^\n]*", "", screen))
    if "if (fall) screen.onFall(now);" not in screen or len(uses) != 2:
        failures.append("updateScreen() must hand the fall to screen.onFall() and nothing else")
    on_fall, tick = screen.find("screen.onFall(now)"), screen.find("screen.tick(")
    if on_fall < 0 or tick < 0 or on_fall > tick:
        failures.append("updateScreen() must call screen.onFall() before screen.tick(): a press in the same pass wins")
    bench = case_body(ino, "BENCH_FALL")
    if bench is None or "screenOnFall();" not in bench or "showDemo" in bench:
        failures.append("serial `of` (BENCH_FALL) must run screenOnFall(), the real path")
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print("PASS the fall is the screen only: screenOnFall() raises one flag, loop() hands it to onFall()")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
