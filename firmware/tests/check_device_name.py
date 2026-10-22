#!/usr/bin/env python3
"""Pins how the sketch uses the model-based Bluetooth name (device_name.h).

Host tests cannot run the SoftDevice or the build script, so this reads the
sources:
  - the name comes from device_name.h (RAILY_DEVICE_NAME), and no other
    "Raily ..." literal is left in the .ino to drift from it;
  - Bluefruit.setName(DEVICE_NAME) is still the one call that sets it;
  - the name goes in the scan response (ScanResponse.addName()), alone, and
    the advertisement (flags + manufacturer marker + 128-bit service UUID)
    is 28 of 31 bytes whatever the name is;
  - build_firmware.sh gives RAILY_SHIP_MODE=1 to --air and to nothing else,
    so the Air image is the one named «Raily Air»;
  - the names agree across the firmware header, the app (PinGATT.swift) and
    the Mac tools (pin_pass_client.APP_NAMES, pin_ble_ota.APP_NAMES_BIN).
It prints the byte counts so a PR can quote them.
"""
import pathlib
import re
import sys

FIRMWARE = pathlib.Path(__file__).resolve().parents[1]
ROOT = FIRMWARE.parents[1]
SKETCH_DIR = FIRMWARE / "RailyPinsP1"
LIMIT = 31
OLD_NAMES = ["Raily Device", "Raily Pin P1"]


def read(path):
    return path.read_text(encoding="utf-8")


def main():
    failures = []
    sketch = read(SKETCH_DIR / "RailyPinsP1.ino")
    header = read(SKETCH_DIR / "device_name.h")

    if '#include "device_name.h"' not in sketch:
        failures.append("the sketch does not include device_name.h")
    if not re.search(r"^static const char DEVICE_NAME\[\] = RAILY_DEVICE_NAME;", sketch, re.M):
        failures.append("DEVICE_NAME is not defined from RAILY_DEVICE_NAME")
    if len(re.findall(r"Bluefruit\.setName\(", sketch)) != 1 or "Bluefruit.setName(DEVICE_NAME);" not in sketch:
        failures.append("Bluefruit.setName(DEVICE_NAME) must be the one call that sets the name")
    stray = re.findall(r'"(Raily [^"]*)"', sketch)
    if stray:
        failures.append(f"a name literal is left in the sketch: {sorted(set(stray))}")

    defined = re.findall(r'#define RAILY_DEVICE_NAME "([^"]+)"', header)
    if defined != ["Raily Air", "Raily P1"]:
        failures.append(f"device_name.h must define exactly Air then P1, got {defined}")
    models = ["Raily P1", "Raily Air"]

    # startAdv(): advertisement and scan response.
    marker_len = None
    start = sketch.find("static void startAdv()")
    end = sketch.find("\n}\n", start) if start >= 0 else -1
    if start < 0 or end < 0:
        failures.append("startAdv() not found in the sketch: re-count the advertising bytes by hand")
    else:
        body = sketch[start:end]
        mfg = re.search(r"kMfg\[\] = \{([^}]*)\}", body)
        if mfg:
            marker_len = len([x for x in mfg.group(1).split(",") if x.strip()])
        else:
            failures.append("startAdv: kMfg not found")
        for call in ["addFlags(", "addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA", "addUuid(railyService.uuid)",
                     "ScanResponse.addName()"]:
            if call not in body:
                failures.append(f"startAdv: {call} is gone, re-count the advertising bytes")
        if body.count("ScanResponse.add") != 1:
            failures.append("startAdv: the scan response must hold the name alone")

    adv = 3 + (2 + (marker_len or 0)) + (2 + 16)  # flags, manufacturer data, 128-bit UUID
    if adv > LIMIT:
        failures.append(f"advertisement is {adv} of {LIMIT} bytes")
    for name in models:
        scan = 2 + len(name)
        if scan > LIMIT:
            failures.append(f"scan response for {name!r} is {scan} of {LIMIT} bytes")
        print(f"  {name!r}: scan response {scan} of {LIMIT} bytes")

    # The monorepo-only checks (the build script, the app, the Mac tools). The
    # public firmware mirror has only firmware/, so there they are skipped.
    tools = ROOT / "hardware" / "tools"
    swift_path = ROOT / "apps" / "raily-pin-companion" / "Sources" / "RailyPinCompanion" / "PinGATT.swift"
    if not (tools / "build_firmware.sh").exists():
        print("  skipped: build script, app and Mac tools are not in this checkout")
    else:
        # build_firmware.sh: ship mode (the Air name) only with --air.
        build = read(tools / "build_firmware.sh")
        code = "\n".join(line for line in build.splitlines() if not line.lstrip().startswith("#"))
        air = re.search(r"^\s*--air\) air_flag=\"([^\"]*)\"", code, re.M)
        diy = re.search(r"^\s*--diy\) diy_flag=\"([^\"]*)\"", code, re.M)
        if not air or "-DRAILY_SHIP_MODE=1" not in air.group(1):
            failures.append("build_firmware.sh: --air no longer defines -DRAILY_SHIP_MODE=1, the Air image would be named P1")
        if not diy or "RAILY_SHIP_MODE" in diy.group(1):
            failures.append("build_firmware.sh: --diy must not define RAILY_SHIP_MODE (the DIY kit is a P1)")
        if len(re.findall(r"RAILY_SHIP_MODE=1", code)) != 1:
            failures.append("build_firmware.sh: RAILY_SHIP_MODE=1 must come from --air only")

        # The same names in the app and the Mac tools.
        swift = read(swift_path)
        for name in models + OLD_NAMES:
            if f'= "{name}"' not in swift:
                failures.append(f"PinGATT.swift does not define the name {name!r}")
        ppc = read(tools / "pin_pass_client.py")
        ota = read(tools / "pin_ble_ota.py")
        for name in models + ["Raily Device"]:
            if f'"{name}"' not in ppc:
                failures.append(f"pin_pass_client.py does not accept {name!r}")
        for name in models + OLD_NAMES:
            if f'b"{name}"' not in ota:
                failures.append(f"pin_ble_ota.py APP_NAMES_BIN does not anchor on {name!r}")

    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print(f"PASS device name: advertisement {adv} of {LIMIT} bytes (flags 3 + marker "
              f"{2 + marker_len} + UUID 18); the name is alone in the scan response; "
              "--air alone builds the Air name; app and Mac tools take every name")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
