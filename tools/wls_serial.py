"""Bounded serial helper for WiFi-LAN-Scanner HIL and production boot checks.

The script never sends credentials and records only the command transcript.
"""

import argparse
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
        ("TAP 20 50 40 80", "WLS-HIL TAP hit=row fire=1 cancel=0 shown=7 face=pressed"),
        ("UI password 0", "WLS-HIL UI ok"),
        ("KEYS", "WLS-HIL KEYS labels=lower face=normal pass=1"),
        ("TAP 10 100 40 80", "WLS-HIL TAP hit=key fire=1 cancel=0 shown=100 face=pressed"),
        ("UI password 1", "WLS-HIL UI ok"),
        ("KEYS", "WLS-HIL KEYS labels=upper face=latched pass=1"),
        ("TAP 10 440 40 80", "WLS-HIL TAP hit=shift fire=1 cancel=0 shown=11 face=latchedpressed"),
        ("PRESERVE", "WLS-HIL PRESERVE preserved=1 mask=1"),
        ("SCAN", "WLS-HIL SCAN pass=1"),
        (
            "DISCOVER",
            "WLS-HIL DISCOVER scan=1 progress=1 pause=1 resume=1 stopSeen=1 complete=1 hosts=2 mac=1 nomac=1 dup=1 reset=1",
        ),
    ]
    ok = True
    for command, expected in steps:
        transcript.append("> " + command)
        port.write((command + "\n").encode("ascii"))
        port.flush()
        timeout = 8 if command in ("SCAN", "DISCOVER") else 4
        got = wait_for(port, lambda line, expected=expected: line == expected, timeout, transcript)
        if got != expected:
            transcript.append("! expected " + expected)
            ok = False
            break
    return ok


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", choices=("hil", "boot"), required=True)
    parser.add_argument("--transcript", required=True)
    args = parser.parse_args()
    transcript = []
    ok = False
    try:
        port = open_port(args.port)
        try:
            if args.mode == "hil":
                ok = run_hil(port, transcript)
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
