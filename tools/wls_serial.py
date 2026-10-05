"""Bounded serial helper for WiFi-LAN-Scanner HIL and production boot checks.

Synthetic HIL and production boot modes send only fixed text commands.
Live mode reads a transient SSID and passphrase from stdin JSON when
--secret-stdin is set, or from the JSON file named by WLS_LIVE_SECRET_FILE.
It sends them once as an unlogged binary frame and never puts that passphrase
on argv, in stdout, or in the transcript.
"""

import argparse
import json
import os
import sys
import time

import serial


def set_rts(port, state):
    port.setRTS(state)
    port.setDTR(port.dtr)


def set_dtr(port, state):
    port.setDTR(state)


def hard_reset(port):
    set_rts(port, True)
    time.sleep(0.2)
    set_rts(port, False)
    time.sleep(0.2)


def open_port(name):
    port = serial.Serial()
    port.port = name
    port.baudrate = 115200
    port.timeout = 0.1
    port.write_timeout = 1
    port.dtr = False
    port.rts = False
    port.open()
    set_dtr(port, False)
    hard_reset(port)
    set_dtr(port, True)
    return port


def read_lines(port, deadline, transcript):
    pending = b""
    found = []
    while time.time() < deadline:
        waiting = port.in_waiting if port.in_waiting else 1
        chunk = port.read(waiting)
        if not chunk:
            continue
        pending += chunk
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = raw.decode("ascii", "replace").replace("\r", "").strip()
            transcript.append(line)
            found.append(line)
    return found


def wait_for(port, predicate, timeout, transcript):
    deadline = time.time() + timeout
    pending = b""
    while time.time() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if not chunk:
            continue
        pending += chunk
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = raw.decode("ascii", "replace").replace("\r", "").strip()
            transcript.append(line)
            if predicate(line):
                return line
    return None


def run_hil(port, transcript):
    ready = wait_for(port, lambda line: line == "WLS-HIL ready", 12, transcript)
    if ready is None:
        hard_reset(port)
        set_dtr(port, True)
        ready = wait_for(port, lambda line: line == "WLS-HIL ready", 12, transcript)
    if ready is None:
        return False
    if not any(line.startswith("WLS psram-alloc=ok ") for line in transcript):
        transcript.append("! psram-alloc-missing")
        return False
    steps = [
        ("PING", "WLS-HIL PONG"),
        ("SELF", "WLS-HIL SELF pass=1"),
        ("UI home", "WLS-HIL UI ok"),
        ("TAP 20 80 40 80", "WLS-HIL TAP hit=find fire=1 cancel=0 shown=1 face=pressed"),
        ("TAP 20 80 40 120", "WLS-HIL TAP hit=find fire=1 cancel=0 shown=-1 face=pressed"),
        ("TAP 213 80 40 80", "WLS-HIL TAP hit=find fire=1 cancel=0 shown=1 face=pressed"),
        ("TAP 214 80 40 80", "WLS-HIL TAP hit=none fire=0 cancel=0 shown=-1 face=normal"),
        ("TAP 0 0 40 80", "WLS-HIL TAP hit=none fire=0 cancel=0 shown=-1 face=normal"),
        ("DRAG 20 80 0 0 30 40", "WLS-HIL DRAG hit=find fire=0 cancel=1"),
        ("UI results", "WLS-HIL UI ok"),
        ("TAP 20 50 40 80", "WLS-HIL TAP hit=row fire=1 cancel=0 shown=200 face=pressed"),
        ("UI password 0", "WLS-HIL UI ok"),
        ("KEYS", "WLS-HIL KEYS labels=lower face=normal pass=1"),
        ("TAP 10 100 40 80", "WLS-HIL TAP hit=key fire=1 cancel=0 shown=100 face=pressed"),
        ("UI password 1", "WLS-HIL UI ok"),
        ("KEYS", "WLS-HIL KEYS labels=upper face=latched pass=1"),
        ("TAP 10 440 40 80", "WLS-HIL TAP hit=shift fire=1 cancel=0 shown=10 face=latchedpressed"),
        ("PRESERVE", "WLS-HIL PRESERVE preserved=1 mask=1"),
        ("SCAN", "WLS-HIL SCAN pass=1"),
        (
            "DISCOVER",
            "WLS-HIL DISCOVER scan=1 progress=1 pause=1 resume=1 stopSeen=1 complete=1 hosts=2 mac=1 nomac=1 dup=1 reset=1",
        ),
        (
            "NAMES",
            "WLS-HIL NAMES named=1 blank=1 kept=1 clipped=1 precedence=1 same=1 ui=1",
        ),
        (
            "OUI",
            "WLS-HIL OUIS known=1 unknown=1 local=1 group=1 kept=1 ui=1 registry=1",
        ),
        ("RESOURCES", "WLS-HIL RESOURCES pass=1"),
        (
            "ACTIONS",
            "WLS-HIL ACTIONS start=1 pause=1 resume=1 stop=1 reset=1 hosts=1 same=1",
        ),
        (
            "PERSIST",
            "WLS-HIL PERSISTS roundtrip=1 comma=1 quote=1 newline=1 secret=0 sd=skipped",
        ),
    ]
    ok = True
    for command, expected in steps:
        transcript.append("> " + command)
        port.write((command + "\n").encode("ascii"))
        port.flush()
        timeout = 8 if command in ("SCAN", "DISCOVER", "NAMES", "OUI", "ACTIONS", "PERSIST", "RESOURCES") else 4
        got = wait_for(port, lambda line, expected=expected: line == expected, timeout, transcript)
        if got != expected:
            transcript.append("! expected " + expected)
            ok = False
            break
    if not ok:
        return False
    transcript.append("> SDPROBE")
    port.write(b"SDPROBE\n")
    port.flush()
    got = wait_for(port, lambda line: line.startswith("WLS-HIL SDPROBE "), 20, transcript)
    if got is None:
        transcript.append("! sdprobe-timeout")
        return False
    if got == "WLS-HIL SDPROBE result=absent display=ok":
        transcript.append("SD_HIL=absent")
        return True
    tokens = got.split()
    if (
        got.startswith("WLS-HIL SDPROBE result=stored ")
        and "match=1" in tokens
        and "removed=1" in tokens
        and "display=ok" in tokens
    ):
        transcript.append("SD_HIL=stored")
        return True
    transcript.append("! sdprobe " + got)
    return False


def run_boot(port, transcript):
    lines = read_lines(port, time.time() + 8, transcript)
    text = "\n".join(lines)
    required = [
        "WLS boot WiFi-LAN-Scanner foundation",
        "WLS display=ok geometry=222x480 expected=222x480",
        "WLS touch probe=0 model=CST226SE",
        "WLS range-selftest=ok cap=256",
        "WLS scanner-selftest=ok discovery=local-arp",
        "WLS ui-selftest=ok ackMs=120 faces=4",
        "WLS mask-selftest=ok preserved=yes",
        "WLS ready discovery=local-arp",
        "WLS psram-alloc=ok",
    ]
    missing = [item for item in required if item not in text]
    saved_ok = ("WLS wifi saved=yes" in text) or ("WLS wifi saved=no" in text)
    hil_present = "WLS-HIL" in text
    if missing or not saved_ok or hil_present:
        transcript.append("! boot-missing " + ",".join(missing))
        if not saved_ok:
            transcript.append("! boot-missing saved-flag")
        if hil_present:
            transcript.append("! boot-contained-hil-marker")
        return False
    return True


def load_live_secret(from_stdin):
    data = None
    raw = b""
    try:
        if from_stdin:
            raw = sys.stdin.buffer.read()
            if raw.endswith(b"\n"):
                raw = raw[:-1]
            if raw.endswith(b"\r"):
                raw = raw[:-1]
            data = json.loads(raw.decode("utf-8"))
        else:
            path = os.environ.get("WLS_LIVE_SECRET_FILE", "")
            if not path:
                raise RuntimeError("live secret file is not configured")
            with open(path, "r", encoding="utf-8") as handle:
                data = json.load(handle)
    except (OSError, json.JSONDecodeError, UnicodeError, RuntimeError):
        raise RuntimeError("live secret shape rejected")
    finally:
        raw = b""
    ssid = data.get("ssid", "") if isinstance(data, dict) else ""
    psk = data.get("psk", "") if isinstance(data, dict) else ""
    data = None
    if not isinstance(ssid, str) or not isinstance(psk, str):
        raise RuntimeError("live secret shape rejected")
    if ssid != "TFMiddle" or len(psk) < 1 or len(psk) > 63:
        raise RuntimeError("live secret shape rejected")
    try:
        ssid.encode("ascii")
        psk.encode("ascii")
    except UnicodeEncodeError:
        raise RuntimeError("live secret shape rejected")
    return ssid, psk


def remember_line(line, secret, transcript):
    if secret and secret in line:
        transcript.append("! secret-line-suppressed")
        return False
    transcript.append(line)
    return True


def run_live(port, transcript, from_stdin):
    ssid, psk = load_live_secret(from_stdin)
    ready = wait_for(port, lambda line: line == "WLS-HIL ready", 12, transcript)
    if ready is None:
        hard_reset(port)
        set_dtr(port, True)
        ready = wait_for(port, lambda line: line == "WLS-HIL ready", 12, transcript)
    if ready is None:
        return False
    transcript.append("> LIVE")
    frame = bytes([len(ssid)]) + ssid.encode("ascii") + bytes([len(psk)]) + psk.encode("ascii")
    port.write(b"LIVE\n")
    port.write(frame)
    del frame
    port.flush()
    deadline = time.time() + 420
    pending = b""
    net_ok = False
    live_ok = False
    clean = True
    while time.time() < deadline:
        chunk = port.read(port.in_waiting or 1)
        if not chunk:
            continue
        pending += chunk
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = raw.decode("ascii", "replace").replace("\r", "").strip()
            if not remember_line(line, psk, transcript):
                clean = False
                continue
            tokens = line.split()
            if line.startswith("WLS-HIL NET ") and "ssid=TFMiddle" in tokens and "inside=1" in tokens and "cap=256" in tokens:
                for token in tokens:
                    if token.startswith("candidates="):
                        number = token.split("=", 1)[1]
                        if number.isdigit() and 1 <= int(number) <= 256:
                            net_ok = True
            if "source=dns" in tokens:
                clean = False
            if line.startswith("WLS-HIL LIVE "):
                live_ok = "pass=1" in tokens
                return net_ok and live_ok and clean
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", choices=("hil", "boot", "live"), required=True)
    parser.add_argument("--transcript", required=True)
    parser.add_argument("--secret-stdin", action="store_true")
    args = parser.parse_args()
    transcript = []
    ok = False
    try:
        port = open_port(args.port)
        try:
            if args.mode == "hil":
                ok = run_hil(port, transcript)
            elif args.mode == "live":
                ok = run_live(port, transcript, args.secret_stdin)
            else:
                ok = run_boot(port, transcript)
                if not ok:
                    hard_reset(port)
                    set_dtr(port, True)
                    transcript.append("! boot-retry")
                    ok = run_boot(port, transcript)
        finally:
            port.close()
    except serial.SerialException as exc:
        transcript.append("! serial " + exc.__class__.__name__)
        ok = False
    with open(args.transcript, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(transcript) + "\n")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
