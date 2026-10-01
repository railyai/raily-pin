#!/usr/bin/env python3
"""The bind window must take the reset reason from the core, not POWER.

The Seeed/Adafruit core's init() (cores/nRF5/wiring.c) reads
NRF_POWER->RESETREAS and clears it before setup(). Reading the register
(or sd_power_reset_reason_get) anywhere in the sketch therefore sees 0,
which bind_window.h reads as a power-on: the bind window would open after
every software reset. Found in review of 0.2.9-qa
(docs/pins/diy-claimless-bind.md). The sketch's own headers and .cpp files
are scanned too; the RESETREAS_* bit constants in bind_window.h are fine.
"""
import pathlib
import re
import sys

sketch_dir = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1"
failures = []
for path in sorted(sketch_dir.glob("*")):
    if path.suffix not in (".ino", ".h", ".cpp"):
        continue
    code = re.sub(r"//[^\n]*", "", path.read_text())
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    for banned in ("NRF_POWER->RESETREAS", "sd_power_reset_reason_get", "sd_power_reset_reason_clr"):
        if banned in code:
            failures.append(f"{path.name} uses {banned}: the core already cleared it")
    if path.suffix == ".ino" and "readResetReason()" not in code:
        failures.append(f"{path.name} never calls readResetReason()")
if failures:
    for failure in failures:
        print("FAIL", failure)
    sys.exit(1)
print("PASS reset reason taken from the core's readResetReason()")
