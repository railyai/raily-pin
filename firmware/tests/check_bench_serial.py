#!/usr/bin/env python3
"""The bench's serial commands must stay out of release builds.

docs/pins/firmware.md «Serial protocol»: s (the sealed secret), w (the bind
window), x (erase every bond) and the o/c parser (bench_serial.h) are bench
aids, compiled only with -DRAILY_BENCH_SERIAL=1 (build_firmware.sh
--bench-serial and the other bench options). A release keeps i, p and r
(the device boot smoke restarts the shipping image with r), and an Air
build keeps [ / ], the button's stand-in. Host tests never compile the
sketch, so this reads it, as check_ble_setup.py does:

- printSealedSecret, eraseBondsFromSerial, openBindWindow("serial w") and
  benchSerialChar( appear only inside `#if RAILY_BENCH_SERIAL`;
- RAILY_BENCH_SERIAL defaults to 0;
- the sketch reads Serial only in loop()'s `while (Serial.available())`
  block, and outside the bench guards that block compares the byte with
  p, i and r only ([ and ] inside `#if RAILY_SHIP_MODE`), so a new command
  cannot slip into a release unnoticed.

A tripwire on the sketch's style, not a proof: it follows #if nesting
literally and does not evaluate conditions beyond the flag names.

    check_bench_serial.py [sketch.ino]
"""
import pathlib
import re
import sys

SKETCH = pathlib.Path(__file__).resolve().parents[1] / "RailyPinsP1" / "RailyPinsP1.ino"
BENCH_ONLY = ("printSealedSecret", "eraseBondsFromSerial", 'openBindWindow("serial w")', "benchSerialChar(")
RELEASE_LETTERS = set("pPiIrR")
SHIP_LETTERS = set("[]")
# Bench builds of their own: their parsers may compare anything.
# RAILY_WATCHDOG_BENCH (serial H hangs loop() on purpose) defaults to
# RAILY_VDD_BENCH, so it is set only by build_firmware.sh --bench.
OTHER_BENCH_FLAGS = ("RAILY_VDD_BENCH", "RAILY_VOICE_BENCH", "RAILY_WATCHDOG_BENCH")


def strip_comments(source):
    """Comments out, line structure kept, string literals untouched."""
    out, i, n = [], 0, len(source)
    while i < n:
        ch = source[i]
        if ch in "\"'":
            j = i + 1
            while j < n and source[j] != ch:
                j += 2 if source[j] == "\\" else 1
            out.append(source[i:j + 1])
            i = j + 1
        elif source.startswith("//", i):
            while i < n and source[i] != "\n":
                i += 1
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * source.count("\n", i, j))
            i = j
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def guarded(condition, flag):
    """True when `condition` holds only with `flag` on (flag, or flag && …)."""
    cond = re.sub(r"\s+", "", condition)
    if "||" in cond or f"!{flag}" in cond:
        return False
    return flag in re.split(r"&&", cond.strip("()"))


def line_contexts(lines):
    """For each line, the set of flags whose guard it sits under."""
    stack, contexts = [], []
    for line in lines:
        directive = re.match(r"\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)", line)
        if directive:
            word, rest = directive.group(1), directive.group(2).strip()
            if word in ("if", "elif"):
                flags = {f for f in ("RAILY_BENCH_SERIAL", "RAILY_SHIP_MODE") + OTHER_BENCH_FLAGS
                         if guarded(rest, f)}
                if word == "if":
                    stack.append(flags)
                elif stack:
                    stack[-1] = flags
            elif word in ("ifdef", "ifndef"):
                stack.append(set())
            elif word == "else" and stack:
                stack[-1] = set()
            elif word == "endif" and stack:
                stack.pop()
        active = set().union(*stack) if stack else set()
        contexts.append(active)
    return contexts


def braced_body(source, start):
    """The text from the brace at or after `start` to its match."""
    i = source.index("{", start)
    depth, j = 1, i + 1
    while depth and j < len(source):
        depth += {"{": 1, "}": -1}.get(source[j], 0)
        j += 1
    return i, j


def main():
    path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else SKETCH
    code = strip_comments(path.read_text())
    lines = code.split("\n")
    contexts = line_contexts(lines)
    failures = []
    if not re.search(r"#ifndef\s+RAILY_BENCH_SERIAL\s*\n\s*#define\s+RAILY_BENCH_SERIAL\s+0\b", code):
        failures.append("RAILY_BENCH_SERIAL does not default to 0")
    for number, (line, flags) in enumerate(zip(lines, contexts), 1):
        for name in BENCH_ONLY:
            if name in line and "RAILY_BENCH_SERIAL" not in flags:
                failures.append(f"line {number}: {name} outside #if RAILY_BENCH_SERIAL")

    loop = re.search(r"^void\s+loop\s*\(\s*\)", code, re.M)
    if not loop:
        failures.append("no loop()")
    else:
        loop_open, loop_close = braced_body(code, loop.end())
        block = code.find("while (Serial.available())", loop_open, loop_close)
        if block < 0:
            failures.append("loop() has no while (Serial.available()) block")
        else:
            body_open, body_close = braced_body(code, block)
            for read in re.finditer(r"\bSerial\.read\s*\(", code):
                if not body_open < read.start() < body_close:
                    failures.append(f"line {code.count(chr(10), 0, read.start()) + 1}: "
                                    "Serial.read outside loop()'s serial block")
            first = code.count("\n", 0, body_open)
            last = code.count("\n", 0, body_close)
            for index in range(first, last + 1):
                flags = contexts[index]
                if "RAILY_BENCH_SERIAL" in flags or flags & set(OTHER_BENCH_FLAGS):
                    continue
                for letter in re.findall(r"\bc\s*==\s*'(\\?.)'", lines[index]):
                    allowed = RELEASE_LETTERS | (SHIP_LETTERS if "RAILY_SHIP_MODE" in flags else set())
                    if letter not in allowed:
                        failures.append(f"line {index + 1}: serial '{letter}' reaches a release build")
    for failure in failures:
        print("FAIL", failure)
    if failures:
        sys.exit(1)
    print("PASS bench serial commands only with RAILY_BENCH_SERIAL; a release answers i, p, r ([ ] on an Air)")


if __name__ == "__main__":
    main()
