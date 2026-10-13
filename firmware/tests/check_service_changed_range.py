#!/usr/bin/env python3
"""Service Changed must start at the first user handle, never at 0x0001.

S140 6.1.1 refuses sd_ble_gatts_service_changed() with
BLE_ERROR_INVALID_ATTR_HANDLE (0x3003) when the range starts inside the
SoftDevice's own GAP or GATT service: "handles must be in the range
populated by the application". 0.2.14-qa to 0.4.0-qa sent 0x0001-0xFFFF,
so no update ever reached a bonded phone's GATT cache (bench pin
rp1-aae1c86b9b4c74e9, 2026-10-09, ble-bonding.md D-7). The start comes
from sd_ble_gatts_initial_user_handle_get(), as in Nordic's peer manager.
Also fails when the phone's write of the Service Changed CCCD does not
raise the send: at the link's encryption the CCCD still reads 0 and the
SoftDevice answers 0x8 (NRF_ERROR_INVALID_STATE); iOS writes it about 0.6 s
later on every link, and only that write can carry the indication (bench,
same day). Host tests never run the SoftDevice, so this reads the sketch
instead.
"""
import pathlib
import re
import sys

sketch_dir = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1"
failures, calls = [], 0
queried = False
for path in sorted(sketch_dir.glob("*")):
    if path.suffix not in (".ino", ".h", ".cpp"):
        continue
    code = re.sub(r"//[^\n]*", "", path.read_text())
    code = re.sub(r"/\*.*?\*/", "", code, flags=re.S)
    if "sd_ble_gatts_initial_user_handle_get(" in code:
        queried = True
    for match in re.finditer(r"sd_ble_gatts_service_changed\(([^,()]*),([^,()]*),", code):
        calls += 1
        start = match.group(2).strip()
        if re.fullmatch(r"(0[xX][0-9a-fA-F]+|\d+)[uUlL]*", start):
            failures.append(f"{path.name}: Service Changed starts at the literal {start}, "
                            "not the first user handle (0x3003 when it is the SoftDevice's own)")
if calls == 0:
    failures.append("no sd_ble_gatts_service_changed() call found: the D-7 indication is gone")
elif not queried:
    failures.append("no sd_ble_gatts_initial_user_handle_get(): the start handle is not the first user handle")
ino = re.sub(r"//[^\n]*", "", (sketch_dir / "RailyPinsP1.ino").read_text())
case = re.search(r"case BLE_GATTS_EVT_WRITE:(.*?)\bbreak;\s*\}", ino, re.S)
if case is None or "serviceChangedCccdHandle" not in case.group(1) \
        or "serviceChangedCheckPending.store(true" not in case.group(1):
    failures.append("onBleEvent: the phone's Service Changed CCCD write does not raise the send (0x8 at encryption)")
# Once per boot, never once per build (2026-10-13): a key that survives a
# reboot (a version hash in the flash table) skips the indication after a
# round trip through 0.3.0, which cannot send it, or after a rebuild under
# the same version with another table; the phone then keeps a stale cache.
due = re.search(r"void sendServiceChangedIfDue\(\)\s*\{(.*?)\n\}", ino, re.S)
if due is None or "serviceChangedBoot.due(" not in due.group(1):
    failures.append("sendServiceChangedIfDue: not decided by serviceChangedBoot.due() (once per boot)")
if "firmwareHash" in ino or "FW_VERSION" in (due.group(1) if due else ""):
    failures.append("Service Changed keyed on the version again: a same-version reflash or a 0.3.0 round trip is skipped")
confirmed = re.search(r'if \(confirmedOn != BLE_CONN_HANDLE_INVALID(.*?)\n  \}', ino, re.S)
if confirmed is None or "serviceChangedBoot.confirm(" not in confirmed.group(1) \
        or "bondTableDirty" in confirmed.group(1):
    failures.append("the phone's confirmation must be recorded in RAM (serviceChangedBoot), never in the flash table")
for failure in failures:
    print("FAIL", failure)
if not failures:
    print(f"PASS {calls} Service Changed call(s) start at the first user handle; the phone's CCCD write sends it; "
          "once per admitted phone per boot, recorded in RAM")
sys.exit(1 if failures else 0)
