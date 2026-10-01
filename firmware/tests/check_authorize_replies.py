#!/usr/bin/env python3
"""Fails when a write-authorize reply could answer SUCCESS with update = 0.

ble_gatts.h (s140 6.1.1): for BLE_GATTS_AUTHORIZE_TYPE_WRITE replies the
`update` bit "must always be set". The SoftDevice refuses a success reply
without it, so the phone gets no ATT answer at all. 0.2.4-qa..0.2.7-qa
answered every accepted pass that way (P4 bench, 2026-09-27); refusals
(update = 0 with an error status) are fine, which is why the refusal-only
GATT probe passed. Host tests never run the SoftDevice, so this reads the
sketch:

- a function building a WRITE reply with update = 0 must never be called
  with BLE_GATT_STATUS_SUCCESS nor set that status itself;
- a function building a WRITE reply with a SUCCESS status must set
  update = 1 and hand the request's bytes back (p_data, len, offset);
- a READ reply with a SUCCESS status must set update = 1 (else the stale
  attribute value goes out, the 0.2.4-qa pass_challenge failure mode).

A tripwire on the sketch's current style, not a proof: it matches the
`params.<op>.field = value` assignments literally and cannot see a status
computed at runtime. The GATT probe with --accept-pass is the proof.
"""
import pathlib
import re
import sys

SKETCH = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1" / "RailyPinsP1.ino"


def functions(source):
    """Yields (name, body) for every top-level static function."""
    for match in re.finditer(r"^static\s+[\w:<>\s\*&]+?\b(\w+)\s*\([^;{]*\)\s*\{", source, re.M):
        depth, i = 1, match.end()
        while depth and i < len(source):
            depth += {"{": 1, "}": -1}.get(source[i], 0)
            i += 1
        yield match.group(1), source[match.end():i]


def main():
    source = SKETCH.read_text()
    failures, write_zero, write_ok, read_ok = [], [], 0, 0
    for name, body in functions(source):
        for kind in ("WRITE", "READ"):
            if f"BLE_GATTS_AUTHORIZE_TYPE_{kind}" not in body:
                continue
            field = kind.lower()
            success = f"params.{field}.gatt_status = BLE_GATT_STATUS_SUCCESS" in body
            update_one = f"params.{field}.update = 1" in body
            update_zero = f"params.{field}.update = 0" in body
            if kind == "WRITE" and update_zero:
                write_zero.append(name)
                if success:
                    failures.append(f"{name}: WRITE reply sets SUCCESS with update = 0")
            if success and not update_one:
                failures.append(f"{name}: {kind} SUCCESS reply without update = 1")
            if kind == "WRITE" and success:
                missing = [f for f in ("p_data", "len", "offset") if f"params.write.{f} =" not in body]
                if missing:
                    failures.append(f"{name}: WRITE SUCCESS reply without {', '.join(missing)}")
                write_ok += update_one
            if kind == "READ" and success:
                read_ok += update_one
    for name in write_zero:
        for call in re.finditer(rf"\b{name}\s*\(([^;]*)\);", source):
            if "BLE_GATT_STATUS_SUCCESS" in call.group(1):
                failures.append(f"{name}(..., BLE_GATT_STATUS_SUCCESS): an update = 0 reply cannot accept")
    if not write_ok:
        failures.append("no WRITE reply accepts with update = 1: accepted passes would go unanswered")
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print(f"PASS authorize replies: {write_ok} write accept(s), {read_ok} read reply(ies) with update = 1; "
              f"refusals via {', '.join(write_zero) or 'none'}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
