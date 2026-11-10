#!/usr/bin/env python3
"""Pins the GATT UUIDs and the server-pass constants across firmware, app and Mac tools.

docs/pins/firmware.md says the UUIDs must be byte-identical in the firmware,
the companion app and the tools. No host test can see that: the firmware
tests never load Swift or the Python tools, and neither of those loads the
sketch. A UUID typed wrong on one side fails only on a real pin, as "the
characteristic is not there" (an optional characteristic is skipped silently
by the app), and a pass result code that drifts turns a refusal into the
wrong retry. So this reads the sources:
  - the sketch's BLEService / BLECharacteristic declarations are the table
    {firmware name -> UUID}; its UUIDs must be distinct and share one base;
  - every UUID-shaped literal in hardware/tools/*.py and in the app's Swift
    sources that is a Raily UUID (7B1E prefix) or one digit-slip away from
    one must be in that table; every short id written in prose (7B1E000C)
    must name a firmware characteristic;
  - a constant the checker can name with certainty (SERVICE, PASS_CHALLENGE,
    PinGATT.passUUID, ...) must hold the UUID of the characteristic it names;
  - PASS_LENGTH and the pass result codes in server_pass.h equal
    pin_pass_client.py (ATT_*) and the app's PinPassWriteResult.fromATTError.
Compared on purpose: UUIDs, PASS_LENGTH, pass result codes, the DFU pass
class and argument. Not compared: screen_state's own ATT codes
(screen_probe.py 0x84-0x86), payload layouts, characteristic properties.
The public firmware mirror has no hardware/tools/ and no apps/: there the
tools and app side is skipped with a note; in the monorepo (build_firmware.sh
present) a missing file is a failure.
"""
import pathlib
import re
import sys

FIRMWARE = pathlib.Path(__file__).resolve().parents[1]
ROOT = FIRMWARE.parents[1]
SKETCH_DIR = FIRMWARE / "RailyPinsP1"
TOOLS_DIR = ROOT / "hardware" / "tools"
APP_SOURCES = ROOT / "apps" / "raily-pin-companion" / "Sources"
PIN_GATT = APP_SOURCES / "RailyPinCompanion" / "PinGATT.swift"
PIN_SERVER_PASS_SWIFT = APP_SOURCES / "RailyPinCompanion" / "PinServerPass.swift"

# A tool constant -> the firmware declaration it names. Only names that are
# certain; a Raily UUID under another name is reported, not guessed.
TOOL_NAMES = {
    "SERVICE": "railyService",
    "DEVICE_INFO": "deviceInfoChr",
    "BUTTON_EVENT": "buttonEventChr",
    "EVENT_ACK": "eventAckChr",
    "PRESS_COMMAND": "pressCommandChr",
    "DEVICE_CONTROL": "deviceControlChr",
    "PASS_CHALLENGE": "passChallengeChr",
    "PASS": "passChr",
    "DEVICE_SECRET": "deviceSecretChr",
    "LINK_SECURITY": "linkSecurityChr",
    "SCREEN_STATE": "screenStateChr",
    "VOICE_STREAM": "voiceStreamChr",
}
# pin_pass_client.ATT_* -> the PassResult enumerator in server_pass.h.
ATT_TO_PASS = {
    "ATT_BAD_MAC": "PASS_BAD_MAC",
    "ATT_UNOWNED": "PASS_UNOWNED",
    "ATT_BAD_FORMAT": "PASS_BAD_FORMAT",
    "ATT_BUSY": "PASS_BUSY",
    "ATT_NO_BLE_UPDATE": "PASS_NO_BLE_UPDATE",
}
# PinGATT constants that are Nordic's legacy DFU service of the bootloader, not the pin's.
SWIFT_NOT_RAILY = {"dfuService", "dfuControlPoint"}
ATT_INSUFFICIENT_ENCRYPTION = 0x0F  # the standard ATT code, not a PassResult
# PinPassWriteResult case -> PassResult enumerator.
SWIFT_PASS_CASES = {
    ".badPass": "PASS_BAD_MAC",
    ".unowned": "PASS_UNOWNED",
    ".badFormat": "PASS_BAD_FORMAT",
    ".busy": "PASS_BUSY",
    ".noBleUpdate": "PASS_NO_BLE_UPDATE",
}

UUID_RE = re.compile(r"(?<![0-9A-Za-z-])[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}(?![0-9A-Za-z-])")
SHORT_RE = re.compile(r"(?<![0-9A-Za-z-])7B1E([0-9A-Fa-f]{4})(?![0-9A-Za-z-])", re.I)
PROSE_PAIR_RE = re.compile(r"([a-z][a-z0-9]*(?:_[a-z0-9]+)+)`?\s*\(\s*7B1E([0-9A-Fa-f]{4})(?![0-9A-Za-z-])", re.I)
NEAR_MISS_DIGITS = 4
# Coverage floors for the monorepo side, from what the tree maps today. Below
# one of them a constant was renamed or its line no longer parses: update
# TOOL_NAMES / ATT_TO_PASS / SWIFT_PASS_CASES (or the regexes) to match.
MIN_TOOL_FILES = 9
MIN_TOOL_CONSTANTS = 17
MIN_PASS_CONSTANTS = 16


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def hamming(a, b):
    return sum(x != y for x, y in zip(a, b))


def snake_to_var(name):
    parts = name.split("_")
    return parts[0] + "".join(p.title() for p in parts[1:]) + "Chr"


def parse_int(text):
    return int(text, 16) if text.lower().startswith("0x") else int(text)


def firmware_table(failures):
    sketch = read(SKETCH_DIR / "RailyPinsP1.ino")
    table = {}
    for m in re.finditer(r'^\s*BLE(?:Service|Characteristic)\s+(\w+)\("([^"]+)"\);', sketch, re.M):
        name, uuid = m.group(1), m.group(2).upper()
        if name in table:
            failures.append(f"firmware: {name} is declared twice")
        table[name] = uuid
    if "railyService" not in table or len(table) < 14:
        failures.append(f"firmware: found {len(table)} BLE declarations (railyService "
                        f"{'present' if 'railyService' in table else 'MISSING'}), the parser lost them")
        return table, None
    by_uuid = {}
    for name, uuid in table.items():
        by_uuid.setdefault(uuid, []).append(name)
    for uuid, names in by_uuid.items():
        if len(names) > 1:
            failures.append(f"firmware: {uuid} is the UUID of {sorted(names)}")
    base = table["railyService"][8:]
    for name, uuid in table.items():
        if not UUID_RE.fullmatch(uuid):
            failures.append(f"firmware: {name} UUID {uuid!r} is not a 128-bit UUID")
        elif uuid[8:] != base:
            failures.append(f"firmware: {name} {uuid} does not share the service base {base}")
    return table, base


def check_uuid_literals(label, text, table, failures):
    """Every Raily-looking UUID, and every short id in prose, must be a firmware one."""
    known = set(table.values())
    known_flat = {u.replace("-", ""): u for u in known}
    known_shorts = {u[:8] for u in known}
    for m in UUID_RE.finditer(text):
        uuid = m.group(0).upper()
        if uuid in known:
            continue
        flat = uuid.replace("-", "")
        near = [(hamming(flat, k), v) for k, v in known_flat.items()]
        dist, closest = min(near)
        if uuid.startswith("7B1E") or dist <= NEAR_MISS_DIGITS:
            failures.append(f"{label}:{line_of(text, m.start())}: {uuid} is not a firmware UUID "
                            f"(closest {closest}, {dist} digits apart)")
    for m in SHORT_RE.finditer(text):
        short = "7B1E" + m.group(1).upper()
        if short not in known_shorts:
            failures.append(f"{label}:{line_of(text, m.start())}: short id {short} is not a firmware UUID")
    for m in PROSE_PAIR_RE.finditer(text):
        var = snake_to_var(m.group(1))
        short = "7B1E" + m.group(2).upper()
        if var in table and table[var][:8] != short:
            failures.append(f"{label}:{line_of(text, m.start())}: prose pairs {m.group(1)} with {short}, "
                            f"the firmware's {var} is {table[var][:8]}")


def check_tool_file(path, label, table, base, ppc_base, failures, notes):
    text = read(path)
    check_uuid_literals(label, text, table, failures)
    file_base = re.search(r'^BASE\s*=\s*"([^"]+)"', text, re.M)
    file_base = file_base.group(1).upper() if file_base else ppc_base
    if file_base and file_base != base and re.search(r'^BASE\s*=', text, re.M):
        failures.append(f"{label}: BASE {file_base} differs from the firmware base {base}")
    assign = re.compile(r'^([A-Z][A-Z0-9_]*)\s*=\s*(?:"([0-9A-Fa-f]{8}-[^"]+)"|"([0-9A-Fa-f]{8})"\s*\+\s*BASE\b)', re.M)
    mapped = 0
    for m in assign.finditer(text):
        name = m.group(1)
        uuid = (m.group(2) or (m.group(3) + (file_base or ""))).upper()
        where = f"{label}:{line_of(text, m.start())}"
        if name in TOOL_NAMES:
            var = TOOL_NAMES[name]
            if var not in table:
                failures.append(f"{where}: {name} names {var}, which the firmware does not declare")
            elif table[var] != uuid:
                failures.append(f"{where}: {name} is {uuid}, the firmware's {var} is {table[var]}")
            else:
                mapped += 1
        elif uuid in table.values() or uuid.startswith("7B1E"):
            notes.append(f"{where}: {name} holds a Raily UUID but is not in the name table (only the UUID was checked)")
    return mapped


def check_swift(table, failures, notes):
    text = read(PIN_GATT)
    mapped = 0
    for m in re.finditer(r'static let (\w+)UUID\s*=\s*"([^"]+)"', text):
        name, uuid = m.group(1), m.group(2).upper()
        if name in SWIFT_NOT_RAILY:
            continue
        var = "railyService" if name == "service" else f"{name}Chr"
        where = f"PinGATT.swift:{line_of(text, m.start())}"
        if var not in table:
            failures.append(f"{where}: {name}UUID names {var}, which the firmware does not declare")
        elif table[var] != uuid:
            failures.append(f"{where}: {name}UUID is {uuid}, the firmware's {var} is {table[var]}")
        else:
            mapped += 1
    if mapped < 14:
        failures.append(f"PinGATT.swift: only {mapped} UUID constants matched the firmware (expected the service and 13 characteristics)")
    for path in sorted(list(APP_SOURCES.rglob("*.swift")) + list(APP_SOURCES.rglob("*.h"))):
        check_uuid_literals(path.relative_to(ROOT).as_posix(), read(path), table, failures)
    return mapped


def check_pass_constants(failures):
    header = read(SKETCH_DIR / "server_pass.h")
    length = re.search(r"PASS_LENGTH\s*=\s*(\d+);", header)
    block = re.search(r"enum PassResult[^{]*\{(.*?)\};", header, re.S)
    if not length or not block:
        failures.append("server_pass.h: PASS_LENGTH or enum PassResult not found")
        return 0
    codes = {m.group(1): int(m.group(2), 16)
             for m in re.finditer(r"^\s*(PASS_[A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+)", block.group(1), re.M)}
    fw_length = int(length.group(1))
    compared = 0

    client = read(TOOLS_DIR / "pin_pass_client.py")
    m = re.search(r"^PASS_LENGTH\s*=\s*(\d+)", client, re.M)
    if not m:
        failures.append("pin_pass_client.py: PASS_LENGTH not found")
    elif int(m.group(1)) != fw_length:
        failures.append(f"pin_pass_client.py: PASS_LENGTH is {m.group(1)}, server_pass.h says {fw_length}")
    else:
        compared += 1
    att = {m.group(1): parse_int(m.group(2))
           for m in re.finditer(r"^(ATT_[A-Z_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", client, re.M)}
    for name, value in att.items():
        if name == "ATT_INSUFFICIENT_ENCRYPTION":
            if value != ATT_INSUFFICIENT_ENCRYPTION:
                failures.append(f"pin_pass_client.py: {name} is {value:#04x}, the standard ATT code is 0x0f")
            continue
        if name not in ATT_TO_PASS:
            failures.append(f"pin_pass_client.py: {name} is not in this checker's name table: map it to a server_pass.h code")
            continue
        want = codes.get(ATT_TO_PASS[name])
        if want is None:
            failures.append(f"server_pass.h: {ATT_TO_PASS[name]} not found")
        elif want != value:
            failures.append(f"pin_pass_client.py: {name} is {value:#04x}, server_pass.h {ATT_TO_PASS[name]} is {want:#04x}")
        else:
            compared += 1
    for name in ATT_TO_PASS:
        if name not in att:
            failures.append(f"pin_pass_client.py: {name} is gone")

    ota = read(TOOLS_DIR / "pin_ble_ota.py")
    m = re.search(r"^PASS_CLASS_CONTROL, PASS_ARG_ENTER_DFU = (0x[0-9A-Fa-f]+), (0x[0-9A-Fa-f]+)", ota, re.M)
    cls = re.search(r"PASS_CLASS_CONTROL\s*=\s*(0x[0-9A-Fa-f]+)", header)
    arg = re.search(r"PASS_ARG_ENTER_DFU\s*=\s*(0x[0-9A-Fa-f]+)", header)
    if not (m and cls and arg):
        failures.append("pin_ble_ota.py / server_pass.h: PASS_CLASS_CONTROL, PASS_ARG_ENTER_DFU not found")
    elif (parse_int(m.group(1)), parse_int(m.group(2))) != (parse_int(cls.group(1)), parse_int(arg.group(1))):
        failures.append(f"pin_ble_ota.py: DFU pass class/arg {m.group(1)}, {m.group(2)}, "
                        f"server_pass.h says {cls.group(1)}, {arg.group(1)}")
    else:
        compared += 2

    swift = read(PIN_SERVER_PASS_SWIFT)
    for case, enumerator in SWIFT_PASS_CASES.items():
        want = codes.get(enumerator)
        m = re.search(rf"case (0x[0-9A-Fa-f]+): {re.escape(case)}\b", swift)
        if want is None:
            failures.append(f"server_pass.h: {enumerator} not found")
        elif not m:
            failures.append(f"PinServerPass.swift: fromATTError has no 'case 0x..: {case}'")
        elif parse_int(m.group(1)) != want:
            failures.append(f"PinServerPass.swift: {case} is {m.group(1)}, server_pass.h {enumerator} is {want:#04x}")
        else:
            compared += 1
    m = re.search(r"case (0x[0-9A-Fa-f]+): \.needsEncryption", swift)
    if not m or parse_int(m.group(1)) != ATT_INSUFFICIENT_ENCRYPTION:
        failures.append("PinServerPass.swift: .needsEncryption must be ATT 0x0F")
    else:
        compared += 1
    for label, pattern, token in (("reboot", r"static let reboot: UInt8 = (0x[0-9A-Fa-f]+)", "PASS_ARG_REBOOT"),
                                  ("enterDFU", r"static let enterDFU: UInt8 = (0x[0-9A-Fa-f]+)", "PASS_ARG_ENTER_DFU")):
        m = re.search(pattern, swift)
        want = re.search(rf"{token}\s*=\s*(0x[0-9A-Fa-f]+)", header)
        if not m or not want:
            failures.append(f"PinPassArg.{label} or {token} not found")
        elif parse_int(m.group(1)) != parse_int(want.group(1)):
            failures.append(f"PinServerPass.swift: PinPassArg.{label} is {m.group(1)}, server_pass.h {token} is {want.group(1)}")
        else:
            compared += 1
    return compared


def main():
    failures, notes = [], []
    table, base = firmware_table(failures)
    summary = f"{len(table)} firmware UUIDs, distinct, one base"

    if not (TOOLS_DIR / "build_firmware.sh").exists():
        print("  skipped: Mac tools, app and server-pass cross-checks are not in this checkout")
    elif base is not None:
        required = [PIN_GATT, PIN_SERVER_PASS_SWIFT, TOOLS_DIR / "pin_pass_client.py", TOOLS_DIR / "pin_ble_ota.py"]
        missing = [p for p in required if not p.exists()]
        for p in missing:
            failures.append(f"missing in the monorepo: {p.relative_to(ROOT)}")
        if not missing:
            ppc_base = None
            m = re.search(r'^BASE\s*=\s*"([^"]+)"', read(TOOLS_DIR / "pin_pass_client.py"), re.M)
            if m:
                ppc_base = m.group(1).upper()
            else:
                failures.append("pin_pass_client.py: BASE not found")
            tool_files = sorted(TOOLS_DIR.glob("*.py"))
            tool_mapped = 0
            for path in tool_files:
                tool_mapped += check_tool_file(path, f"hardware/tools/{path.name}", table, base, ppc_base, failures, notes)
            app_mapped = check_swift(table, failures, notes)
            compared = check_pass_constants(failures)
            for what, got, floor, table_name in (
                    ("tool files read", len(tool_files), MIN_TOOL_FILES, "hardware/tools/*.py"),
                    ("named tool constants mapped", tool_mapped, MIN_TOOL_CONSTANTS, "TOOL_NAMES"),
                    ("pass constants compared", compared, MIN_PASS_CONSTANTS, "ATT_TO_PASS / SWIFT_PASS_CASES")):
                if got < floor:
                    failures.append(f"coverage: {got} {what}, expected at least {floor}: a constant was renamed "
                                    f"or its line no longer parses; update {table_name} in this checker")
            summary += (f"; {len(tool_files)} tool files ({tool_mapped} named constants) and "
                        f"{app_mapped} PinGATT constants agree; {compared} pass constants agree")

    for note in notes:
        print("  note:", note)
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print(f"PASS protocol constants: {summary}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
