#!/usr/bin/env python3
"""A DFU-entry pass must never arm the jump on a bootloader that cannot finish
a BLE update (bootloader_kind.h).

The P1 prototype runs the stock Seeed bootloader (UF2 Bootloader 0.6.1,
advertises AdaDFU). A phone OTA jumped into it on 2026-10-13; the app never
found it and the pin stayed parked: no app, no USB, no timeout. The pass
handler runs on the SoftDevice, so host tests cannot reach it; this reads the
sketch instead. Fails when:
- the refusal (bootloaderTakesBleUpdate) does not come before the pass
  action is handed to loop() in onPassAuthorize;
- it answers anything but PASS_NO_BLE_UPDATE;
- the kind is not detected in setup() before Bluefruit.begin() (with the
  SoftDevice on, ACL is a restricted peripheral) or not after an ACL check.
"""
import pathlib
import re
import sys

sketch = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1" / "RailyPinsP1.ino"
code = re.sub(r"//[^\n]*", "", sketch.read_text())
failures = []

handler = re.search(r"static void onPassAuthorize\(.*?\n\}", code, re.S)
if handler is None:
    failures.append("onPassAuthorize not found")
else:
    body = handler.group(0)
    guard = body.find("bootloaderTakesBleUpdate(bootloaderKind)")
    handoff = body.find("pendingPassAction.store(")
    if guard < 0:
        failures.append("onPassAuthorize: no bootloaderTakesBleUpdate() check for the DFU-entry pass")
    elif handoff >= 0 and guard > handoff:
        failures.append("onPassAuthorize: the bootloader check comes after the action is handed to loop()")
    else:
        branch = body[guard:guard + 400]
        if "PASS_ARG_ENTER_DFU" not in body[max(0, guard - 200):guard] or "PASS_NO_BLE_UPDATE" not in branch \
                or "return;" not in branch:
            failures.append("onPassAuthorize: a stock bootloader must get PASS_NO_BLE_UPDATE and return")

setup = re.search(r"void setup\(\) \{(.*?)\n\}", code, re.S)
if setup is None:
    failures.append("setup() not found")
else:
    body = setup.group(1)
    detect = body.find("bootloaderKind = detectBootloader()")
    begin = body.find("Bluefruit.begin(")
    if detect < 0 or begin < 0 or detect > begin:
        failures.append("setup(): detectBootloader() must run before Bluefruit.begin()")
detector = re.search(r"BootloaderKind detectBootloader\(\) \{(.*?)\n\}", code, re.S)
if detector is None or "NRF_ACL" not in detector.group(1) \
        or detector.group(1).find("NRF_ACL") > detector.group(1).find("classifyBootloader("):
    failures.append("detectBootloader(): the ACL check must come before the image is read")

for failure in failures:
    print("FAIL", failure)
if not failures:
    print("PASS a DFU-entry pass on a bootloader without BLE updates is refused (0x87) before any jump; "
          "the kind is read before the SoftDevice, after an ACL check")
sys.exit(1 if failures else 0)
