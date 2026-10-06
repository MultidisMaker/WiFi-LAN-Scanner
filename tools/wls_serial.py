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


def contains_legacy_scan_dir(transcript):
    return any("/LANScanner/scans/" in line for line in transcript)


def read_until(port, predicate, timeout, transcript, secret=None, pending=b""):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if b"\n" not in pending:
            chunk = port.read(port.in_waiting or 1)
            if not chunk:
                continue
            pending += chunk
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = raw.decode("ascii", "replace").replace("\r", "").strip()
            if secret and secret in line:
                transcript.append("! secret-line-suppressed")
                return None, b""
            transcript.append(line)
            if predicate(line):
                return line, pending
    return None, pending


def wait_for(port, predicate, timeout, transcript):
    line, _pending = read_until(port, predicate, timeout, transcript)
    return line


def resource_line_healthy(line):
    if not line.startswith("WLS resource "):
        return False
    fields = {}
    for token in line.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        fields[key] = value
    try:
        heap = int(fields["heap"])
        block = int(fields["block"])
        psram = int(fields["psram"])
        free_psram = int(fields["freePsram"])
    except (KeyError, ValueError):
        return False
    return heap > 0 and block > 0 and psram > 0 and free_psram > 0 and free_psram <= psram


def result_count(line):
    marker = '"count":'
    if marker not in line:
        return -1
    digits = []
    for char in line.split(marker, 1)[1]:
        if char.isdigit():
            digits.append(char)
        else:
            break
    if not digits:
        return -1
    return int("".join(digits))


def send_frame(port, body, transcript):
    wire = "@R1 " + body
    transcript.append("> " + wire)
    port.write((wire + "\n").encode("ascii"))
    port.flush()


def exercise_remote(port, transcript, pending=b"", live_ip=None, secret=None, gateway_ip=None):
    def exact(expected):
        return lambda line, expected=expected: line == expected

    def step(body, predicate, timeout):
        nonlocal pending
        send_frame(port, body, transcript)
        line, pending = read_until(port, predicate, timeout, transcript, secret=secret, pending=pending)
        return line

    if step('{"v":1,"op":"HELLO"}', exact('@R1 {"v":1,"op":"HELLO_ACK","ok":1,"link":"usb","support":1}'), 4) is None:
        transcript.append("! remote-hello")
        return False, pending
    if step('{"v":1,"op":"HELLO"}', exact('@R1 {"v":1,"op":"ERR","reason":"session"}'), 4) is None:
        transcript.append("! remote-session")
        return False, pending

    def state_ok(line):
        if not line.startswith('@R1 {"v":1,"op":"STATE"'):
            return False
        lowered = line.lower()
        return "password" not in lowered and "psk" not in lowered and "passphrase" not in lowered

    if step('{"v":1,"op":"GET_STATE"}', state_ok, 4) is None:
        transcript.append("! remote-state")
        return False, pending

    if live_ip is not None:
        start = len(transcript)
        end = step('{"v":1,"op":"GET_RESULTS"}', lambda line: line.startswith("@R1 ") and '"op":"RESULT_END"' in line, 15)
        if end is None or result_count(end) < 1:
            transcript.append("! remote-results")
            return False, pending
        if not any(('"ip":"%s"' % live_ip) in line for line in transcript[start:]):
            transcript.append("! remote-results-ip")
            return False, pending
        if not gateway_ip:
            transcript.append("! remote-gateway")
            return False, pending
        custom_body = '{"v":1,"op":"ACTION","name":"custom","ip":"%s"}' % gateway_ip
        sequence = (
            ("hosts", None, "pressed"),
            ("row", '{"v":1,"op":"ACTION","name":"row","index":0}', "pressed"),
            ("back", None, "pressed"),
            ("settings", None, "pressed"),
            ("service", None, "pressed"),
            ("basic", None, ("pressed", "latchedpressed")),
            ("detailed", None, ("pressed", "latchedpressed")),
            ("common", None, ("pressed", "latchedpressed")),
            ("back", None, "pressed"),
            ("range", None, "pressed"),
            ("count64", None, ("pressed", "latchedpressed")),
            ("custom", custom_body, ("pressed", "latchedpressed")),
            ("back", None, "pressed"),
            ("back", None, "pressed"),
        )
    else:
        sequence = (
            ("hosts", None, "pressed"),
            ("next", None, "pressed"),
            ("prev", None, "pressed"),
            ("row", '{"v":1,"op":"ACTION","name":"row","index":0}', "pressed"),
            ("back", None, "pressed"),
            ("start", None, "pressed"),
            ("pause", None, "pressed"),
            ("resume", None, "pressed"),
            ("stop", None, "pressed"),
            ("reset", None, "pressed"),
            ("settings", None, "pressed"),
            ("service", None, "pressed"),
            ("basic", None, ("pressed", "latchedpressed")),
            ("detailed", None, ("pressed", "latchedpressed")),
            ("common", None, ("pressed", "latchedpressed")),
            ("back", None, "pressed"),
            ("range", None, "pressed"),
            ("count64", None, ("pressed", "latchedpressed")),
            ("custom", '{"v":1,"op":"ACTION","name":"custom","ip":"10.1.2.10"}', ("pressed", "latchedpressed")),
            ("windownext", None, "pressed"),
            ("windowprev", None, "pressed"),
            ("automatic", None, ("pressed", "latchedpressed")),
            ("count256", None, ("pressed", "latchedpressed")),
            ("back", None, "pressed"),
            ("back", None, "pressed"),
        )

    def remote_action(name, body=None, face="pressed"):
        nonlocal pending
        start = len(transcript)
        payload = body if body is not None else '{"v":1,"op":"ACTION","name":"%s"}' % name
        expected = '@R1 {"v":1,"op":"ACTION_RESULT","name":"%s","ok":1}' % name
        if step(payload, exact(expected), 6) is None:
            transcript.append("! remote-action " + name)
            return False
        window = transcript[start:]
        faces = face if isinstance(face, tuple) else (face,)
        armed_lines = tuple("WLS ui ack arm control=%s face=%s ms=120" % (name, item) for item in faces)
        skipped = "WLS ui ack skip action=%s" % name
        fired = "WLS ui ack fire control=%s" % name
        if any(line in window for line in armed_lines):
            got, pending = read_until(
                port,
                lambda line, fired=fired: line == fired,
                8,
                transcript,
                secret=secret,
                pending=pending,
            )
            if got != fired:
                transcript.append("! remote-ack-fire " + name)
                return False
            return True
        if skipped in window:
            return True
        transcript.append("! remote-ack-missing " + name)
        return False

    def wait_scan(tokens, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = step('{"v":1,"op":"GET_STATE"}', state_ok, 8)
            if line and any(('"scan":"%s"' % token) in line for token in tokens):
                return True
            time.sleep(0.3)
        transcript.append("! remote-scan-wait " + ",".join(tokens))
        return False

    for name, body, face in sequence:
        if not remote_action(name, body, face):
            return False, pending

    if live_ip is not None:
        if not remote_action("start"):
            return False, pending
        if not wait_scan(("SCANNING",), 20):
            return False, pending
        if not remote_action("pause"):
            return False, pending
        if not wait_scan(("PAUSED",), 8):
            return False, pending
        if not remote_action("resume"):
            return False, pending
        if not wait_scan(("SCANNING",), 8):
            return False, pending
        if not remote_action("stop"):
            return False, pending
        if not wait_scan(("COMPLETE", "IDLE"), 15):
            return False, pending
        restore = (
            ("settings", None, "pressed"),
            ("range", None, "pressed"),
            ("automatic", None, ("pressed", "latchedpressed")),
            ("count256", None, ("pressed", "latchedpressed")),
            ("back", None, "pressed"),
            ("back", None, "pressed"),
            ("settings", None, "pressed"),
            ("service", None, "pressed"),
            ("common", None, ("pressed", "latchedpressed")),
            ("back", None, "pressed"),
            ("back", None, "pressed"),
        )
        for name, body, face in restore:
            if not remote_action(name, body, face):
                return False, pending

    if live_ip is None:
        empty = step('{"v":1,"op":"GET_RESULTS"}', exact('@R1 {"v":1,"op":"RESULT_END","count":0}'), 4)
        if empty is None:
            transcript.append("! remote-empty-results")
            return False, pending

    if step("{", exact('@R1 {"v":1,"op":"ERR","reason":"malformed"}'), 4) is None:
        transcript.append("! remote-malformed")
        return False, pending
    if step('{"v":1,"op":"NOPE"}', exact('@R1 {"v":1,"op":"ERR","reason":"unknown"}'), 4) is None:
        transcript.append("! remote-unknown")
        return False, pending
    if step("A" * 360, exact('@R1 {"v":1,"op":"ERR","reason":"oversize"}'), 4) is None:
        transcript.append("! remote-oversize")
        return False, pending
    if step('{"v":1,"op":"PING","id":3}', exact('@R1 {"v":1,"op":"PONG","id":3}'), 4) is None:
        transcript.append("! remote-recover")
        return False, pending
    if step('{"v":1,"op":"GOODBYE"}', exact('@R1 {"v":1,"op":"GOODBYE","ok":1}'), 4) is None:
        transcript.append("! remote-goodbye")
        return False, pending
    if step('{"v":1,"op":"GET_STATE"}', exact('@R1 {"v":1,"op":"ERR","reason":"closed"}'), 4) is None:
        transcript.append("! remote-closed")
        return False, pending

    if live_ip is not None:
        return True, pending

    transcript.append("> PING")
    port.write(b"PING\n")
    port.flush()
    pong, pending = read_until(port, lambda line: line == "WLS-HIL PONG", 4, transcript, pending=pending)
    if pong is None:
        transcript.append("! remote-hil-pong")
        return False, pending
    transcript.append("> UI home")
    port.write(b"UI home\n")
    port.flush()
    home, pending = read_until(port, lambda line: line == "WLS-HIL UI ok", 4, transcript, pending=pending)
    if home is None:
        transcript.append("! remote-ui-home")
        return False, pending
    transcript.append("> TAP 20 80 40 80")
    port.write(b"TAP 20 80 40 80\n")
    port.flush()
    tap, pending = read_until(
        port,
        lambda line: line == "WLS-HIL TAP hit=find fire=1 cancel=0 shown=1 face=pressed",
        4,
        transcript,
        pending=pending,
    )
    if tap is None:
        transcript.append("! remote-tap")
        return False, pending
    transcript.append("> RESOURCES")
    port.write(b"RESOURCES\n")
    port.flush()
    resources, pending = read_until(port, lambda line: line == "WLS-HIL RESOURCES pass=1", 8, transcript, pending=pending)
    if resources is None or not any(resource_line_healthy(line) for line in transcript):
        transcript.append("! remote-resources")
        return False, pending
    return True, pending


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
            "SERVICES",
            "WLS-HIL SERVICES open=2 closed=1 timeout=1 error=1 once=1 pause=1 resume=1 stop=1 reset=1 planned=6 done=5 targets=2 skipped=1 heap=1",
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
        timeout = 8 if command in ("SCAN", "DISCOVER", "NAMES", "OUI", "ACTIONS", "PERSIST", "RESOURCES", "SERVICES") else 4
        got = wait_for(port, lambda line, expected=expected: line == expected, timeout, transcript)
        if got != expected:
            transcript.append("! expected " + expected)
            ok = False
            break
    if not ok:
        return False
    if not any(line.startswith("WLS-HIL PERSIST path=/WiFi-LAN-Scanner/scans/scan-") for line in transcript):
        transcript.append("! persist-path")
        return False
    if contains_legacy_scan_dir(transcript):
        transcript.append("! legacy-scan-path")
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
    else:
        tokens = got.split()
        if not (
            got.startswith("WLS-HIL SDPROBE result=stored ")
            and "match=1" in tokens
            and "removed=1" in tokens
            and "display=ok" in tokens
            and "path=/WiFi-LAN-Scanner/scans/a011-wls-sdhil.csv" in tokens
        ):
            transcript.append("! sdprobe " + got)
            return False
        transcript.append("SD_HIL=stored")
    transcript.append("> RANGE")
    port.write(b"RANGE\n")
    port.flush()
    ranged = wait_for(port, lambda line: line == "WLS-HIL RANGE pass=1", 8, transcript)
    if ranged != "WLS-HIL RANGE pass=1":
        transcript.append("! range")
        return False
    remote_ok, _pending = exercise_remote(port, transcript)
    if not remote_ok or contains_legacy_scan_dir(transcript):
        if contains_legacy_scan_dir(transcript):
            transcript.append("! legacy-scan-path")
        return False
    return True


def run_production_remote(port, transcript):
    ready = wait_for(port, lambda line: line == "WLS ready discovery=local-arp", 12, transcript)
    if ready is None or any("WLS-HIL" in line for line in transcript):
        transcript.append("! production-remote-ready")
        return False

    def exact(expected):
        return lambda line, expected=expected: line == expected

    pending = b""

    def step(body, predicate, timeout):
        nonlocal pending
        send_frame(port, body, transcript)
        line, pending = read_until(port, predicate, timeout, transcript, pending=pending)
        return line

    if step('{"v":1,"op":"HELLO"}', exact('@R1 {"v":1,"op":"HELLO_ACK","ok":1,"link":"usb","support":1}'), 8) is None:
        transcript.append("! production-hello")
        return False

    def state_home(line):
        return line.startswith('@R1 {"v":1,"op":"STATE"') and '"screen":"home"' in line and "password" not in line.lower() and "psk" not in line.lower()

    state = step('{"v":1,"op":"GET_STATE"}', state_home, 8)
    if state is None:
        state = step('{"v":1,"op":"GET_STATE"}', state_home, 8)
    if state is None:
        transcript.append("! production-state")
        return False
    if step('{"v":1,"op":"PING","id":7}', exact('@R1 {"v":1,"op":"PONG","id":7}'), 8) is None:
        if step('{"v":1,"op":"PING","id":7}', exact('@R1 {"v":1,"op":"PONG","id":7}'), 8) is None:
            transcript.append("! production-ping")
            return False

    start = len(transcript)
    if step('{"v":1,"op":"ACTION","name":"find"}', exact('@R1 {"v":1,"op":"ACTION_RESULT","name":"find","ok":1}'), 6) is None:
        transcript.append("! production-find")
        return False
    if "WLS ui ack arm control=find face=pressed ms=120" not in transcript[start:]:
        transcript.append("! production-find-ack")
        return False
    fired, pending = read_until(port, lambda line: line == "WLS ui ack fire control=find", 4, transcript, pending=pending)
    if fired is None:
        transcript.append("! production-find-fire")
        return False

    deadline = time.time() + 25
    saw_results = False
    while time.time() < deadline and not saw_results:
        remaining = deadline - time.time()
        if remaining < 0.2:
            break
        line = step('{"v":1,"op":"GET_STATE"}', lambda item: item.startswith('@R1 {"v":1,"op":"STATE"'), remaining)
        if line is None:
            break
        if '"screen":"results"' in line:
            saw_results = True
            break
        if '"wifi":"failed"' in line:
            transcript.append("! production-scan-failed")
            return False
        time.sleep(0.5)
    if not saw_results:
        transcript.append("! production-results-screen")
        return False

    start = len(transcript)
    if step('{"v":1,"op":"ACTION","name":"back"}', exact('@R1 {"v":1,"op":"ACTION_RESULT","name":"back","ok":1}'), 6) is None:
        transcript.append("! production-back")
        return False
    if "WLS ui ack arm control=back face=pressed ms=120" not in transcript[start:]:
        transcript.append("! production-back-ack")
        return False
    fired, pending = read_until(port, lambda line: line == "WLS ui ack fire control=back", 4, transcript, pending=pending)
    if fired is None:
        transcript.append("! production-back-fire")
        return False
    home = step('{"v":1,"op":"GET_STATE"}', lambda line: line.startswith('@R1 {"v":1,"op":"STATE"') and '"screen":"home"' in line, 8)
    if home is None:
        home = step('{"v":1,"op":"GET_STATE"}', lambda line: line.startswith('@R1 {"v":1,"op":"STATE"') and '"screen":"home"' in line, 8)
    if home is None:
        transcript.append("! production-home")
        return False
    if step('{"v":1,"op":"GOODBYE"}', exact('@R1 {"v":1,"op":"GOODBYE","ok":1}'), 8) is None:
        transcript.append("! production-goodbye")
        return False
    touch, _pending = read_until(port, lambda line: line == "WLS touch ready=1", 8, transcript, pending=pending)
    if touch is None or any("WLS-HIL" in line for line in transcript):
        transcript.append("! production-touch")
        return False
    return True


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
    hold, pending = read_until(
        port,
        lambda line: line.startswith("WLS-HIL LIVE ") or "Guru Meditation" in line or "panic'ed" in line,
        420,
        transcript,
        secret=psk,
    )
    if hold is None or not hold.startswith("WLS-HIL LIVE hold=") or contains_legacy_scan_dir(transcript):
        if contains_legacy_scan_dir(transcript):
            transcript.append("! legacy-scan-path")
        return False
    net_ok = False
    persist_path = False
    persist_stored = False
    observed_ip = None
    gateway_ip = None
    seen = 0
    for line in transcript:
        tokens = line.split()
        if "source=dns" in tokens:
            transcript.append("! live-dns")
            return False
        if line.startswith("WLS-HIL NET ") and "ssid=TFMiddle" in tokens and "inside=1" in tokens and "cap=256" in tokens:
            for token in tokens:
                if token.startswith("candidates="):
                    number = token.split("=", 1)[1]
                    if number.isdigit() and 1 <= int(number) <= 256:
                        net_ok = True
                if token.startswith("gw=") and token.count(".") == 3:
                    gateway_ip = token.split("=", 1)[1]
        if line.startswith("WLS-HIL HOST "):
            for token in tokens:
                if token.startswith("ip=") and token != "ip=none" and observed_ip is None:
                    observed_ip = token.split("=", 1)[1]
        if line.startswith("WLS sd path=/WiFi-LAN-Scanner/scans/scan-"):
            persist_path = True
        if line == "WLS-HIL PERSIST live=stored":
            persist_stored = True
        if line.startswith("WLS-HIL LIVE hold="):
            for token in tokens:
                if token.startswith("seen=") and token.split("=", 1)[1].isdigit():
                    seen = int(token.split("=", 1)[1])
    if not net_ok or not persist_path or not persist_stored or observed_ip is None or gateway_ip is None or seen < 1:
        transcript.append("! live-before-remote")
        return False
    remote_ok, pending = exercise_remote(
        port, transcript, pending=pending, live_ip=observed_ip, secret=psk, gateway_ip=gateway_ip
    )
    if not remote_ok:
        return False
    transcript.append("> LIVECLOSE")
    port.write(b"LIVECLOSE\n")
    port.flush()
    closed, _pending = read_until(
        port,
        lambda line: line.startswith("WLS-HIL LIVE "),
        30,
        transcript,
        secret=psk,
        pending=pending,
    )
    if closed is None or contains_legacy_scan_dir(transcript):
        if contains_legacy_scan_dir(transcript):
            transcript.append("! legacy-scan-path")
        return False
    after_reset = any(line.startswith("WLS resource phase=after-reset ") and resource_line_healthy(line) for line in transcript)
    if not after_reset:
        transcript.append("! live-resource")
        return False
    return "pass=1" in closed.split()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--mode", choices=("hil", "boot", "live", "production-remote"), required=True)
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
                if not ok:
                    transcript.append("! hil-retry")
                    hard_reset(port)
                    set_dtr(port, True)
                    ok = run_hil(port, transcript)
            elif args.mode == "live":
                ok = run_live(port, transcript, args.secret_stdin)
            elif args.mode == "production-remote":
                ok = run_production_remote(port, transcript)
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
