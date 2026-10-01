#!/usr/bin/env python3
"""Fails when a characteristic's authorize callback is set after begin().

Bluefruit (Seeed core 1.1.13) copies rd_auth/wr_auth into the SoftDevice
attribute only inside BLECharacteristic::begin(); a set*AuthorizeCallback()
after begin() is silently ignored. On the P4 bench (2026-09-27) that left
pass_challenge returning uninitialised memory and pass/device_control
writes ACKed without any check. Host tests never run the SoftDevice, so
this reads the sketch instead.
"""
import pathlib
import re
import sys

SKETCH = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1" / "RailyPinsP1.ino"
# The characteristics whose checks live in an authorize callback: without
# one, writes are ACKed unchecked, so a missing callback fails too.
REQUIRED = {"deviceControlChr", "passChallengeChr", "passChr", "screenStateChr"}


def main():
    source = SKETCH.read_text()
    failures, checked, seen = [], 0, set()
    for match in re.finditer(r"^\s*(\w+)\.set(Read|Write)AuthorizeCallback\(", source, re.M):
        name = match.group(1)
        begins = list(re.finditer(rf"^\s*{re.escape(name)}\.begin\(\);", source, re.M))
        begin = begins[0] if len(begins) == 1 else None
        checked += 1
        seen.add(name)
        if len(begins) > 1:
            failures.append(f"{name}: {len(begins)} begin() calls, cannot check the order")
            continue
        if begin is None:
            failures.append(f"{name}: authorize callback but no {name}.begin()")
        elif begin.start() < match.start():
            failures.append(f"{name}: set{match.group(2)}AuthorizeCallback after begin() is ignored")
    for name in sorted(REQUIRED - seen):
        failures.append(f"{name}: no authorize callback, its writes or reads go unchecked")
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print(f"PASS {checked} authorize callbacks set before begin()")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
