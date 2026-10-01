#!/usr/bin/env python3
"""Fails when screen code could run before BLE is up.

The pre-flash gate (.devin/skills/raily-device/SKILL.md): a fault before
Bluefruit.begin() and advertising never reaches USB or BLE, and bricks the
only bench pin. So the OLED is probed and driven from loop() only:
setup() must not touch Wire, U8g2 or the renderer, and no display object
may be built as a global (its constructor would run before setup()).
The vibration motor pin is started after advertising too. Host tests
never boot the board, so this reads the sources.
"""
import pathlib
import re
import sys

SKETCH = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1"
FORBIDDEN_IN_SETUP = re.compile(r"\b(oled\w*|Wire\w*|U8G2\w*|u8g2\w*|updateScreen|updateOutputs)\b")


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


def main():
    failures = []
    ino = (SKETCH / "RailyPinsP1.ino").read_text()
    setup = function_body(ino, "void setup() {")
    loop = function_body(ino, "void loop() {")
    if setup is None or loop is None:
        failures.append("setup() or loop() not found in RailyPinsP1.ino")
    else:
        for match in FORBIDDEN_IN_SETUP.finditer(setup):
            failures.append(f"setup() uses {match.group(1)}: the screen starts from loop(), after BLE")
        if "updateOutputs();" not in loop:
            failures.append("loop() does not call updateOutputs(): the screen is never driven")
    # Every call sits on the loop() path: loop -> updateOutputs -> updateScreen -> oledProbe/oledShow.
    outputs = function_body(ino, "void updateOutputs() {") or ""
    screen = function_body(ino, "void updateScreen(uint32_t now, uint8_t ack, bool linkUp) {") or ""
    for name, home in (("updateOutputs", loop or ""), ("updateScreen", outputs), ("oledProbe", screen),
                       ("oledShow", screen)):
        calls = len(re.findall(rf"\b{name}\(", ino)) - len(re.findall(rf"\bvoid {name}\(", ino))
        if calls != len(re.findall(rf"\b{name}\(", home)):
            failures.append(f"{name}() is called outside its loop() path")
    # The motor pin (D0) is a plain output set in setup() only after
    # advertising starts, and nowhere else.
    if setup is not None:
        adv, motor = setup.find("startAdv();"), setup.find("startMotorPin();")
        if motor < 0 or adv < 0 or motor < adv:
            failures.append("setup() must call startMotorPin() after startAdv()")
    if len(re.findall(r"\bstartMotorPin\(\);", ino)) != 1:
        failures.append("startMotorPin() must be called exactly once (from setup())")
    render = (SKETCH / "oled_render.cpp").read_text()
    for match in re.finditer(r"^(?:static\s+)?U8G2_\w+\s+\w+\s*[({;]", render, re.M):
        failures.append(f"oled_render.cpp builds a global display ({match.group(0).strip()}): build it in oledProbe()")
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print("PASS the OLED starts from loop(), after BLE; setup() never touches the bus")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
