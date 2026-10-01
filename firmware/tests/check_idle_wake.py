#!/usr/bin/env python3
"""Fails when a flag for loop() could wait for the sleep backstop.

Keyring Air sleep (docs/pins/air-power-risks.md, idle_sleep.h): in a
-DRAILY_IDLE_SLEEP=1 build loop() blocks on its task notification. The BLE
task and the Ada callback task raise flags that loop() drains (std::atomic
`pending*` / `*Pending` and the Service Changed handle); each such store
outside the loop task must be followed by RAILY_WAKE_LOOP() in the same
function, or the work would wait up to the 1 s backstop (a DFU drain, a
reseal, the screen). idleSleepMs() must look at every one of those flags
and at every loop() deadline, so a pass never blocks over pending work.
Host tests never boot the board, so this reads the sources.
"""
import pathlib
import re
import sys

SKETCH = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1" / "RailyPinsP1.ino"
# Functions that only ever run on the loop task (setup() shares it): a flag
# they raise is drained later in the same pass or the next one.
LOOP_TASK_ONLY = {"loop", "serviceHeldPress", "runPendingPassAction", "serviceBonds", "updateScreen",
                  "updateOutputs", "enforceBondCap", "onButton"}
# Raised by the BLE task but never drained as work: they only need the wake
# (the LED and the screen follow the link).
WAKE_ONLY = ["connected"]
# The deadlines and busy states loop() acts on; idleSleepMs() must see each.
MUST_SEE = ["screenMailboxFull", "heldPress", "spareNonceReady", "rebootAtMs", "dropLinkAtMs",
            "bondCapDueMs", "bindWindow", "feedbackBusy(feedback)", "screenBusy(screen)", "rhythmBusy(rhythm)",
            "buttonEdges", "buttonDebounce"]


def functions(source):
    """Top-level function bodies by name (a definition starts at column 0)."""
    out = {}
    for match in re.finditer(r"^(?=[A-Za-z_])(?:static[ \t]+)?(?:__attribute__\(\(\w+\)\)[ \t]+)?[\w:<>*& \t]+?\b(\w+)\(([^;{]*)\)\s*\{",
                             source, re.M):
        depth = 0
        for j in range(match.end() - 1, len(source)):
            depth += {"{": 1, "}": -1}.get(source[j], 0)
            if depth == 0:
                out.setdefault(match.group(1), "")
                out[match.group(1)] += source[match.end() - 1:j + 1]
                break
    return out


def main():
    failures = []
    ino = SKETCH.read_text()
    flags = [name for name in re.findall(r"^static std::atomic<[^>]+> (\w+)\(", ino, re.M)
             if name.startswith("pending") or name.endswith("Pending") or name == "serviceChangedConfirmedHandle"]
    if len(flags) < 8:
        failures.append(f"found only {len(flags)} loop() flags: the declaration pattern changed")
    bodies = functions(ino)
    for name, body in bodies.items():
        code = re.sub(r"//[^\n]*", "", body)
        raised = [f for f in flags + WAKE_ONLY if re.search(rf"\b{f}\.store\((?!PASS_ACTION_NONE|0,)", code)]
        if raised and name not in LOOP_TASK_ONLY and "RAILY_WAKE_LOOP();" not in code:
            failures.append(f"{name}() raises {', '.join(raised)} but never calls RAILY_WAKE_LOOP()")
    sleep = re.sub(r"//[^\n]*", "", bodies.get("idleSleepMs", "")) or None
    if sleep is None:
        failures.append("idleSleepMs() not found")
    else:
        for name in flags + MUST_SEE:
            if not re.search(rf"(?<!\w){re.escape(name)}(?!\w)", sleep):
                failures.append(f"idleSleepMs() never looks at {name}")
    loop = bodies.get("loop", "")
    tail = loop.rstrip().rstrip("}").rstrip()
    if not tail.endswith("#if RAILY_IDLE_SLEEP\n  idleBlock();\n#endif"):
        failures.append("loop() must end with idleBlock() (after the serial commands)")
    if not re.search(r"#if RAILY_IDLE_SLEEP\n.*\n#define RAILY_WAKE_LOOP\(\) wakeLoop\(\)\n#else\n"
                     r"#define RAILY_WAKE_LOOP\(\) \(\(void\)0\)\n#endif", ino, re.S):
        failures.append("RAILY_WAKE_LOOP() must be a no-op outside RAILY_IDLE_SLEEP (the P1 binary stays the same)")
    for failure in failures:
        print("FAIL", failure)
    if not failures:
        print(f"PASS every loop() flag ({len(flags)}) wakes the loop task and idleSleepMs() sees it")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
