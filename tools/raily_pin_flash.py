#!/usr/bin/env python3
"""Flash a Raily Pin (Seeed XIAO nRF52840 / nRF52840 Sense).

One small, readable script that an AI agent (or a person) runs step by step.
It never needs administrator rights, never runs code it downloads, and only
talks to the network to fetch firmware files from the Raily download host.

Steps, in the order flash.md runs them:

  detect       find the board over USB; refuse anything that is not a XIAO nRF52840
  fetch        download the release files and check their SHA-256
  bootloader   read INFO_UF2.TXT from the XIAO drive (after a double-tap on RESET)
  install-bootloader   copy the OTAFIX bootloader update onto the drive
  flash        copy the firmware .uf2 onto the drive
  flash-serial alternative: flash the firmware .zip over USB serial (adafruit-nrfutil)
  verify       send `i` over USB serial and check the firmware version
  report       write a draft GitHub Discussions post after a failure

Trust model: the expected SHA-256 of every file comes from releases.json in
this repository (GitHub, github.com/railyai/raily-pin). The files come from
https://download.railyai.com/pins/. A file whose hash differs from
releases.json is deleted and never copied to the board, so a tampered
download host cannot pass the check on its own. The host's own SHA256SUMS,
when present, must agree as well.

Only the Python standard library is needed, plus pyserial for `verify` and
adafruit-nrfutil for `flash-serial` (tools/requirements.txt, hash-pinned).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import string
import struct
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

DOWNLOAD_ORIGIN = "https://download.railyai.com"
DOWNLOAD_PREFIX = DOWNLOAD_ORIGIN + "/pins/"

XIAO_VID = 0x2886
APP_PID = 0x8044  # firmware running ("XIAO nRF52840")
BOOT_PID = 0x0045  # UF2 bootloader (double-tap RESET)
BOOT_PID_PLAIN = 0x0044  # UF2 bootloader of the plain (non-Sense) XIAO
ALLOWED = {
    (XIAO_VID, APP_PID): "app",
    (XIAO_VID, BOOT_PID): "bootloader",
    (XIAO_VID, BOOT_PID_PLAIN): "bootloader",
}

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_FLAG_FAMILY = 0x00002000
NRF52840_FAMILY = 0xADA52840  # application images
BOOTLOADER_FAMILY = 0xD663823C  # "update-*.uf2" bootloader self-update images

DEVICE_ID_RE = re.compile(r"rp1-[0-9a-f]{16}")
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$")
FILENAME_RE = re.compile(r"^[A-Za-z0-9._-]+$")

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
DEFAULT_WORKDIR = Path.home() / ".raily-pin"


class Stop(Exception):
    """A step failed; the message is shown to the person as-is."""


def say(msg: str) -> None:
    print(msg, flush=True)


def emit(result: dict) -> None:
    """Last line of every step: machine-readable JSON for the agent."""
    print("RESULT " + json.dumps(result, sort_keys=True), flush=True)


# ---------------------------------------------------------------- releases


def load_releases(path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        raise Stop(f"{path.name}: unknown schema {data.get('schema')!r}")
    return data


def pick_release(releases: dict, version: str | None) -> dict:
    version = version or releases["latest"]
    for rel in releases["firmware"]:
        if rel["version"] == version:
            return rel
    known = ", ".join(r["version"] for r in releases["firmware"])
    raise Stop(f"firmware {version} is not in releases.json (known: {known})")


def board_variant(info: dict, mount: Path | None = None) -> str:
    """'sense' or 'plain'. Factory boards say Seeed_XIAO_nRF52840_Sense,
    OTAFIX says nRF52840-SeeedXiaoSense-v1; the drive name is a second hint."""
    board_id = (info.get("board_id") or "").lower()
    drive = (mount.name if mount else "").upper()
    if "sense" in board_id or drive == "XIAO-SENSE":
        return "sense"
    if "xiao" in board_id:
        return "plain"
    raise Stop(f"unknown board {info.get('board_id')!r}: only the XIAO nRF52840 and XIAO nRF52840 Sense are supported")


def bootloader_entry(releases: dict, variant: str) -> dict:
    for entry in releases["bootloader"]["variants"]:
        if entry["variant"] == variant:
            return entry
    raise Stop(f"no OTAFIX bootloader listed for the {variant} board")


def is_otafix(releases: dict, version: str | None) -> bool:
    return bool(version) and version.startswith(releases["bootloader"]["accept_prefix"])


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def check_file_name(name: str) -> None:
    if not FILENAME_RE.match(name) or name.startswith("."):
        raise Stop(f"refusing odd file name {name!r}")


def download(url: str, dest: Path) -> None:
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.netloc != "download.railyai.com" or not parsed.path.startswith("/pins/"):
        raise Stop(f"refusing to download from {url} (only {DOWNLOAD_PREFIX} is allowed)")
    req = urllib.request.Request(url, headers={"User-Agent": "raily-pin-flash/1", "Cache-Control": "no-cache"})
    tmp = dest.with_suffix(dest.suffix + ".part")
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:  # noqa: S310 - https + host checked above
            final = urllib.parse.urlsplit(resp.geturl())
            if final.scheme != "https" or final.netloc != "download.railyai.com":
                raise Stop(f"download of {url} was redirected to {resp.geturl()}; refusing")
            with tmp.open("wb") as out:
                shutil.copyfileobj(resp, out)
    except urllib.error.HTTPError as exc:
        raise Stop(f"download of {url} failed with HTTP {exc.code}; the release file is not available. Nothing was written to the board.") from exc
    except urllib.error.URLError as exc:
        raise Stop(f"could not reach download.railyai.com ({exc.reason}); check the internet connection and run fetch again.") from exc
    tmp.replace(dest)


def fetch_verified(name: str, expected: str, workdir: Path, local_dir: Path | None) -> Path:
    check_file_name(name)
    dest = workdir / "files" / name
    dest.parent.mkdir(parents=True, exist_ok=True)
    if local_dir is not None:
        shutil.copyfile(local_dir / name, dest)
        say(f"  copied {name} from {local_dir} (maintainer dry run)")
    else:
        url = DOWNLOAD_PREFIX + name
        say(f"  downloading {url}")
        download(url, dest)
    actual = sha256_file(dest)
    if actual != expected:
        dest.unlink(missing_ok=True)
        raise Stop(
            f"SHA-256 mismatch for {name}: expected {expected} (releases.json on GitHub), got {actual}. "
            "The file was deleted and nothing was written to the board."
        )
    say(f"  SHA-256 OK  {name}  {actual}")
    return dest


def cross_check_host_sums(names: dict[str, str], workdir: Path, local_dir: Path | None) -> None:
    """The host's SHA256SUMS must agree with releases.json (a second, weaker signal)."""
    if local_dir is not None:
        sums_path = local_dir / "SHA256SUMS"
        if not sums_path.exists():
            say("  (no SHA256SUMS in the local folder; skipped the host cross-check)")
            return
        text = sums_path.read_text(encoding="utf-8")
    else:
        dest = workdir / "files" / "SHA256SUMS"
        download(DOWNLOAD_PREFIX + "SHA256SUMS", dest)
        text = dest.read_text(encoding="utf-8")
    host = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2:
            host[parts[1].lstrip("*")] = parts[0].lower()
    for name, expected in names.items():
        if name not in host:
            raise Stop(f"the download host's SHA256SUMS does not list {name}")
        if host[name] != expected:
            raise Stop(f"the download host's SHA256SUMS disagrees with releases.json for {name}")
    say("  download host SHA256SUMS agrees with releases.json")


# ---------------------------------------------------------------- USB


def list_xiao_ports() -> tuple[list[dict], list[dict]]:
    try:
        from serial.tools import list_ports  # type: ignore
    except ImportError as exc:
        raise Stop("pyserial is missing: install tools/requirements.txt into the venv first") from exc
    matches, others = [], []
    for p in list_ports.comports():
        if p.vid is None:
            continue
        entry = {
            "port": p.device,
            "vid": f"0x{p.vid:04x}",
            "pid": f"0x{p.pid:04x}",
            "product": p.product or p.description,
        }
        mode = ALLOWED.get((p.vid, p.pid))
        if mode:
            entry["mode"] = mode
            matches.append(entry)
        else:
            others.append(entry)
    return matches, others


def cmd_detect(args) -> dict:
    matches, others = list_xiao_ports()
    if not matches:
        hint = (
            "No XIAO nRF52840 found. Check that the cable carries data (try another one), "
            "plug the board in directly, then run detect again."
        )
        if others:
            seen = ", ".join(f"{o['product']} ({o['vid']}:{o['pid']})" for o in others)
            hint += f" USB serial devices seen: {seen}. None of them is a XIAO nRF52840, so they are left alone."
        raise Stop(hint)
    if len(matches) > 1:
        raise Stop("More than one XIAO nRF52840 is connected. Unplug all but the one you want to flash.")
    board = matches[0]
    say(f"Found {board['product']} on {board['port']} ({board['vid']}:{board['pid']}, {board['mode']} mode).")
    return {"ok": True, **board}


# ---------------------------------------------------------------- UF2 drive


def candidate_mounts() -> list[Path]:
    system = platform.system()
    roots: list[Path] = []
    if system == "Darwin":
        roots = [Path("/Volumes")]
    elif system == "Linux":
        user = os.environ.get("USER", "")
        roots = [Path("/media") / user, Path("/run/media") / user, Path("/media"), Path("/mnt")]
    elif system == "Windows":
        return [Path(f"{letter}:\\") for letter in string.ascii_uppercase[3:]]
    mounts = []
    for root in roots:
        try:
            mounts.extend(p for p in root.iterdir() if p.is_dir())
        except OSError:
            continue
    return mounts


def parse_info_uf2(text: str) -> dict:
    info = {"raw_first_line": text.splitlines()[0].strip() if text.strip() else ""}
    m = re.search(r"UF2 Bootloader\s+(\S+)", text)
    info["bootloader"] = m.group(1) if m else None
    for key, field in (("Model", "model"), ("Board-ID", "board_id"), ("SoftDevice", "softdevice"), ("Date", "date")):
        m = re.search(rf"^{key}:\s*(.+?)\s*$", text, re.MULTILINE)
        info[field] = m.group(1) if m else None
    return info


def find_xiao_drive() -> tuple[Path, dict] | None:
    for mount in candidate_mounts():
        info_path = mount / "INFO_UF2.TXT"
        try:
            if not info_path.is_file():
                continue
            text = info_path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        info = parse_info_uf2(text)
        if "nRF52840" in (info.get("model") or "") or "nRF52840" in (info.get("board_id") or ""):
            if "XIAO" in (info.get("model") or "") or "XIAO" in (info.get("board_id") or ""):
                return mount, info
    return None


def wait_for_drive(timeout: float) -> tuple[Path, dict]:
    deadline = time.monotonic() + timeout
    while True:
        found = find_xiao_drive()
        if found:
            return found
        if time.monotonic() > deadline:
            raise Stop(
                "The XIAO drive did not appear. Double-tap the RESET button quickly (two taps within half a second); "
                "the green LED should pulse and a drive named XIAO-SENSE (or similar) should appear. Then run this step again."
            )
        time.sleep(1)


def cmd_bootloader(args) -> dict:
    releases = load_releases(args.releases)
    say("Looking for the XIAO drive (INFO_UF2.TXT)...")
    mount, info = wait_for_drive(args.timeout)
    required = releases["bootloader"]["version"]
    say(f"Drive {mount}: {info['raw_first_line']}")
    say(f"Board: {info.get('model')} / {info.get('board_id')}; SoftDevice: {info.get('softdevice')}")
    variant_name = board_variant(info, mount)
    variant = bootloader_entry(releases, variant_name)
    status = "otafix" if is_otafix(releases, info.get("bootloader")) else "needs_update"
    if status == "otafix":
        say(f"Bootloader {info['bootloader']} is already the required {required}.")
    else:
        say(f"Bootloader {info.get('bootloader')} is not {required}: over-the-air updates need {required}.")
    return {
        "ok": True,
        "drive": str(mount),
        "status": status,
        "required": required,
        "variant": variant_name,
        "variant_file": variant["file"],
        **{k: info.get(k) for k in ("bootloader", "model", "board_id", "softdevice", "date")},
    }


def check_uf2(path: Path, family_id: int = NRF52840_FAMILY) -> int:
    """Sanity-check a UF2 file: magic numbers and the expected family on every block."""
    data = path.read_bytes()
    if not data or len(data) % 512:
        raise Stop(f"{path.name} is not a UF2 file (size {len(data)})")
    blocks = len(data) // 512
    for i in range(blocks):
        m0, m1, flags, _addr, _size, _no, total, family = struct.unpack_from("<8I", data, i * 512)
        if m0 != UF2_MAGIC_START0 or m1 != UF2_MAGIC_START1:
            raise Stop(f"{path.name}: block {i} has bad UF2 magic")
        if not flags & UF2_FLAG_FAMILY or family != family_id:
            raise Stop(f"{path.name}: block {i} has family 0x{family:08x}, expected 0x{family_id:08x}")
        if total != blocks:
            raise Stop(f"{path.name}: block count mismatch ({total} vs {blocks})")
    return blocks


def copy_to_drive(src: Path, mount: Path) -> None:
    dest = mount / src.name
    say(f"Copying {src.name} to {mount} ...")
    try:
        with src.open("rb") as fin, dest.open("wb") as fout:
            shutil.copyfileobj(fin, fout, length=1 << 16)
            fout.flush()
            try:
                os.fsync(fout.fileno())
            except OSError:
                pass
    except OSError as exc:
        # The board reboots as soon as the last block lands, so the drive
        # disappears mid-close. That is the normal, successful outcome.
        say(f"  the drive went away during the copy ({exc.__class__.__name__}); that is normal when the board reboots")


def expected_files(args) -> tuple[dict, dict]:
    releases = load_releases(args.releases)
    rel = pick_release(releases, args.version)
    return releases, rel


def cmd_fetch(args) -> dict:
    releases, rel = expected_files(args)
    names = {rel["uf2"]["file"]: rel["uf2"]["sha256"], rel["zip"]["file"]: rel["zip"]["sha256"]}
    for variant in releases["bootloader"]["variants"]:
        names[variant["file"]] = variant["sha256"]
    say(f"Firmware {rel['version']} and bootloader {releases['bootloader']['version']}: checking against releases.json")
    paths = {n: str(fetch_verified(n, h, args.workdir, args.local_dir)) for n, h in names.items()}
    if not args.no_host_sums:
        cross_check_host_sums(names, args.workdir, args.local_dir)
    check_uf2(Path(paths[rel["uf2"]["file"]]), NRF52840_FAMILY)
    for variant in releases["bootloader"]["variants"]:
        check_uf2(Path(paths[variant["file"]]), BOOTLOADER_FAMILY)
    return {"ok": True, "version": rel["version"], "files": paths}


def verified_local(name: str, expected: str, workdir: Path) -> Path:
    path = workdir / "files" / name
    if not path.is_file():
        raise Stop(f"{name} has not been downloaded yet: run the fetch step first")
    if sha256_file(path) != expected:
        path.unlink(missing_ok=True)
        raise Stop(f"{name} changed on disk since it was verified; deleted it. Run fetch again.")
    return path


def cmd_install_bootloader(args) -> dict:
    releases = load_releases(args.releases)
    mount, info = wait_for_drive(args.timeout)
    required = releases["bootloader"]["version"]
    if is_otafix(releases, info.get("bootloader")):
        say(f"Bootloader {info.get('bootloader')} is already OTAFIX; nothing to do.")
        return {"ok": True, "changed": False, "bootloader": info.get("bootloader")}
    variant = bootloader_entry(releases, board_variant(info, mount))
    src = verified_local(variant["file"], variant["sha256"], args.workdir)
    check_uf2(src, BOOTLOADER_FAMILY)
    if not args.yes:
        raise Stop("install-bootloader writes the bootloader. Re-run with --yes after the person has agreed.")
    say(
        "Installing the OTAFIX bootloader. Do NOT unplug the board until the drive comes back "
        "(about 10-30 seconds): an interrupted bootloader write is the one way to make recovery hard."
    )
    copy_to_drive(src, mount)
    say(
        "Waiting for the drive to come back with the new bootloader. If it has not come back after "
        "30 seconds, double-tap RESET again (the new bootloader may start in Bluetooth update mode)."
    )
    time.sleep(3)
    mount2, info2 = wait_for_drive(max(args.timeout, 120))
    if not is_otafix(releases, info2.get("bootloader")):
        raise Stop(
            f"After the update the drive reports bootloader {info2.get('bootloader')!r}, not {required}. "
            "Do not unplug; ask for help (report step)."
        )
    say(f"Bootloader is now {info2.get('bootloader')}.")
    return {"ok": True, "changed": True, "bootloader": info2.get("bootloader"), "drive": str(mount2)}


def cmd_flash(args) -> dict:
    releases, rel = expected_files(args)
    src = verified_local(rel["uf2"]["file"], rel["uf2"]["sha256"], args.workdir)
    check_uf2(src, NRF52840_FAMILY)
    mount, info = wait_for_drive(args.timeout)
    required = releases["bootloader"]["version"]
    if not is_otafix(releases, info.get("bootloader")) and not args.allow_stock_bootloader:
        raise Stop(
            f"The bootloader is {info.get('bootloader')!r}, not {required}. Install the bootloader first "
            "(install-bootloader), otherwise the pin can never update over the air."
        )
    if not args.yes:
        raise Stop("flash writes the firmware. Re-run with --yes after the person has agreed.")
    copy_to_drive(src, mount)
    say("The board is restarting into the Raily Pin firmware.")
    return {"ok": True, "version": rel["version"], "method": "uf2"}


def cmd_flash_serial(args) -> dict:
    _releases, rel = expected_files(args)
    src = verified_local(rel["zip"]["file"], rel["zip"]["sha256"], args.workdir)
    board = cmd_detect(args)
    nrfutil = shutil.which("adafruit-nrfutil", path=str(Path(sys.executable).parent)) or shutil.which("adafruit-nrfutil")
    if not nrfutil:
        raise Stop("adafruit-nrfutil is missing: install tools/requirements.txt into the venv first")
    if not args.yes:
        raise Stop("flash-serial writes the firmware. Re-run with --yes after the person has agreed.")
    cmd = [nrfutil, "dfu", "serial", "--package", str(src), "-p", board["port"], "-b", "115200", "--singlebank"]
    if board["mode"] == "app":
        cmd += ["--touch", "1200"]
    say("Running: " + " ".join(cmd))
    import subprocess

    # click (inside adafruit-nrfutil) aborts under an ASCII locale.
    env = dict(os.environ, PYTHONUTF8="1", LC_ALL=os.environ.get("LC_ALL") or "C.UTF-8", LANG=os.environ.get("LANG") or "C.UTF-8")
    proc = subprocess.run(cmd, check=False, env=env)
    if proc.returncode != 0:
        raise Stop(f"adafruit-nrfutil exited with {proc.returncode}")
    return {"ok": True, "version": rel["version"], "method": "serial"}


def read_device_info(port: str, timeout: float) -> dict:
    import serial  # type: ignore

    deadline = time.monotonic() + timeout
    buf = ""
    with serial.Serial(port, 115200, timeout=0.5) as ser:
        time.sleep(0.5)
        ser.reset_input_buffer()
        last_send = 0.0
        while time.monotonic() < deadline:
            if time.monotonic() - last_send > 2:
                ser.write(b"i\n")
                last_send = time.monotonic()
            buf += ser.read(512).decode("utf-8", errors="replace")
            for line in buf.splitlines():
                line = line.strip()
                if line.startswith("{") and '"device_id"' in line:
                    try:
                        data = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if DEVICE_ID_RE.fullmatch(str(data.get("device_id", ""))):
                        return data
    raise Stop("The pin did not answer `i` over USB serial. Unplug and replug it, then run verify again.")


def cmd_verify(args) -> dict:
    _releases, rel = expected_files(args)
    deadline = time.monotonic() + args.timeout
    board = None
    while time.monotonic() < deadline:
        matches, _ = list_xiao_ports()
        apps = [m for m in matches if m["mode"] == "app"]
        if len(apps) == 1:
            board = apps[0]
            break
        time.sleep(1)
    if board is None:
        raise Stop("The board did not come back as a XIAO nRF52840 running firmware. Unplug and replug it, then run verify again.")
    info = read_device_info(board["port"], 20)
    fw = info.get("fw")
    if fw != rel["version"]:
        raise Stop(f"The pin reports firmware {fw!r}, expected {rel['version']}.")
    say(f"Pin answers: firmware {fw}, device ID {info['device_id']}.")
    say("Keep the device ID private (it is shown in the app); do not post it publicly.")
    return {"ok": True, "fw": fw, "device_id": info["device_id"], "port": board["port"]}


# ---------------------------------------------------------------- report


def scrub(text: str) -> str:
    text = DEVICE_ID_RE.sub("rp1-<hidden>", text)
    home = str(Path.home())
    if home and home != "/":
        text = text.replace(home, "~")
    user = os.environ.get("USER") or os.environ.get("USERNAME")
    if user and len(user) > 2:
        text = re.sub(rf"\b{re.escape(user)}\b", "<user>", text)
    text = re.sub(r"[\w.+-]+@[\w-]+\.[\w.-]+", "<email>", text)
    return text


def cmd_report(args) -> dict:
    lines = [
        f"**Step that failed:** {scrub(args.step)}",
        f"**Board:** {scrub(args.board or 'unknown')}",
        f"**Bootloader:** {scrub(args.bootloader or 'unknown')}",
        f"**Firmware (target / on the pin):** {scrub(args.firmware or 'unknown')}",
        f"**Computer:** {platform.system()} {platform.release()} ({platform.machine()}), Python {platform.python_version()}",
        f"**Agent:** {scrub(args.agent or 'unknown')}",
        "",
        "**What happened:**",
        "",
        "```text",
        scrub(args.error.strip())[:4000],
        "```",
        "",
        "_Draft written by the Raily Pin flashing tool. The pin's device ID is left out on purpose;"
        " if support needs it, send it through Help in the Raily Pin app._",
    ]
    body = "\n".join(lines) + "\n"
    out = args.workdir / "discussion-draft.md"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(body, encoding="utf-8")
    title = f"Flashing failed at '{scrub(args.step)}' on {platform.system()}"
    url = "https://github.com/railyai/raily-pin/discussions/new?" + urllib.parse.urlencode(
        {"category": "flashing-help", "title": title}
    )
    say(body)
    say(f"Draft saved to {out}.")
    say(f"Review it, then post it yourself at: {url}")
    return {"ok": True, "draft": str(out), "new_discussion_url": url, "title": title}


# ---------------------------------------------------------------- main


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--releases", type=Path, default=REPO / "releases.json", help="expected checksums (default: releases.json next to tools/)")
    p.add_argument("--workdir", type=Path, default=DEFAULT_WORKDIR, help="where downloads and the report draft go (default: ~/.raily-pin)")
    p.add_argument("--version", help="firmware version (default: 'latest' in releases.json)")
    p.add_argument("--timeout", type=float, default=90, help="seconds to wait for the board or its drive")
    p.add_argument("--yes", action="store_true", help="the person agreed to write to the board")
    p.add_argument(
        "--local-dir",
        type=Path,
        help="MAINTAINERS ONLY: take files from this folder instead of download.railyai.com (checksums still come from releases.json)",
    )
    p.add_argument("--no-host-sums", action="store_true", help="skip the download host's SHA256SUMS cross-check (releases.json is still enforced)")
    p.add_argument("--allow-stock-bootloader", action="store_true", help=argparse.SUPPRESS)
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("detect")
    sub.add_parser("fetch")
    sub.add_parser("bootloader")
    sub.add_parser("install-bootloader")
    sub.add_parser("flash")
    sub.add_parser("flash-serial")
    sub.add_parser("verify")
    r = sub.add_parser("report")
    r.add_argument("--step", required=True)
    r.add_argument("--error", required=True)
    r.add_argument("--board")
    r.add_argument("--bootloader")
    r.add_argument("--firmware")
    r.add_argument("--agent")
    return p


COMMANDS = {
    "detect": cmd_detect,
    "fetch": cmd_fetch,
    "bootloader": cmd_bootloader,
    "install-bootloader": cmd_install_bootloader,
    "flash": cmd_flash,
    "flash-serial": cmd_flash_serial,
    "verify": cmd_verify,
    "report": cmd_report,
}


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    args.workdir = args.workdir.expanduser()
    if args.version and not VERSION_RE.match(args.version):
        emit({"ok": False, "step": args.cmd, "error": f"bad version {args.version!r}"})
        return 2
    try:
        result = COMMANDS[args.cmd](args)
    except Stop as exc:
        say(f"STOPPED: {exc}")
        emit({"ok": False, "step": args.cmd, "error": str(exc)})
        return 1
    except Exception as exc:  # noqa: BLE001 - report anything unexpected in one line
        say(f"STOPPED: unexpected error: {exc.__class__.__name__}: {exc}")
        emit({"ok": False, "step": args.cmd, "error": f"{exc.__class__.__name__}: {exc}"})
        return 1
    emit(result)
    return 0


if __name__ == "__main__":
    sys.exit(main())
