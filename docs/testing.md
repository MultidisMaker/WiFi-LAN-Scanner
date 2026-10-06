# Testing

Host-native tests and a test-only serial HIL mode cover the shared firmware logic. The normal production image does not include the HIL protocol. Discovery tests use a fake backend and do not transmit.

## Shared logic

These files are compiled into both the firmware and the host tests:

- `src/UiPress.cpp` — press, release, drag-off, acknowledgement, faces, glyphs, and masking
- `src/NetMath.cpp` — IPv4 prefix, usable-host count, and address formatting
- `src/CandidatePlan.cpp` — on-subnet candidate selection and the 256-host cap
- `src/HostInventory.cpp` — in-memory de-duplicated observations
- `src/PasswordBuffer.cpp` — typed password state, including Shift preservation
- `src/ScannerController.cpp` — scanner state machine and discovery scheduling
- `src/UiModel.cpp` — control geometry and synthetic gestures
- `src/NameRecord.cpp` — hostname sanitizing, source precedence, and the host-row detail line
- `src/UiRender.cpp` — dirty-region decisions and address-progress percent
- `src/ServiceProfile.cpp` — Service Scan profile tokens, labels, and invalid-value fallback

`src/ScanClock.cpp` supplies `scanNow()` from `millis()` on the device. Host tests supply their own `scanNow()` so transitions can be stepped without waiting. `src/NetworkRange.cpp` and `src/ScannerUi.cpp` adapt the shared helpers to Arduino types and the panel. They are not part of the host build.

## Commands

From the repository root:

```text
powershell -NoProfile -File tools\Invoke-WlsHostTests.ps1
powershell -NoProfile -File tools\Invoke-WlsRegression.ps1
```

The host command runs `pio test -e native`. The regression command runs that suite, builds the production image, identifies the connected board, uploads the HIL image, runs the synthetic serial script, then uploads the production image again and checks its boot log. It returns a non-zero exit code if any required step fails. If the HIL image was uploaded, the script still attempts to restore production firmware.

`-Live` adds one association, one bounded ARP sweep, mDNS hostname enrichment, and offline manufacturer enrichment for hosts that sweep already observed. It runs after the synthetic serial pass and before production restore. The script looks up the `TFMiddle` passphrase in the canonical Agentic credential vault with `Get-AgenticKeePassCredential.ps1` and writes that JSON to the live helper's standard input. Do not pass the passphrase as an argument, and do not commit it.

```text
powershell -NoProfile -File tools\Invoke-WlsRegression.ps1 -Live
```

`tools/Invoke-WlsRegression.ps1` accepts `-WorkDir` for verbose logs and `-EvidenceDir` for the short transcripts. Neither path is committed.

## Production and test builds

| Environment | Command | HIL protocol |
| --- | --- | --- |
| `lilygo-t-display-s3-pro` | `pio run -e lilygo-t-display-s3-pro` | absent (`WLS_TEST_MODE` is 0) |
| `lilygo-t-display-s3-pro-hil` | `pio run -e lilygo-t-display-s3-pro-hil` | present (`-DWLS_TEST_MODE=1`) |
| `native` | `pio test -e native` | not built |

The HIL environment extends the production environment, repeats `-DWLS_OUI_EMBEDDED` and `-DBOARD_HAS_PSRAM`, and adds the test-mode macro. PlatformIO replaces `build_flags` on the child environment, so those flags are written on both lines. The parent also sets `board_build.arduino.memory_type = qio_opi`, and the child inherits it. `platformio.ini` does not set `upload_port`. The regression script selects the single Espressif USB serial device whose parent id is `VID_303A&PID_1001` and USB serial `80:65:99:A0:3E:70`, then checks `flash_id` for that MAC and a 16MB flash. Zero or multiple matches stop the run.

The production `.bin` and `.elf` must not contain the ASCII token `WLS-HIL`. The HIL image must contain it. HIL replies use that prefix. The synthetic commands are `PING`, `SELF`, `UI`, `TAP`, `DRAG`, `KEYS`, `PRESERVE`, `SCAN`, `DISCOVER`, `NAMES`, `OUI`, `RESOURCES`, `ACTIONS`, `PERSIST`, and `SDPROBE`. They do not join Wi-Fi, print passwords, or offer a general shell. `DISCOVER` runs the scanner against synthetic hosts inside `FakeDiscoveryBackend`. It does not call `WiFi.begin`. `NAMES` applies deterministic fake hostnames, including a missing name, to that same in-memory inventory and checks the host-row model. `OUI` classifies a fixed fixture of global, unknown, local, and group MACs and checks the host-row manufacturer line. It also requires the compiled IEEE index to contain at least 30,000 entries. The fixture names are not IEEE assignments. `RESOURCES` checks the resource-line formatter and prints one live heap sample. `ACTIONS` runs the same scanner transitions from a control id and from a direct `AppAction`. `PERSIST` checks CSV escaping in RAM and prints `sd=skipped`. It does not mount or format a card. `SDPROBE` mounts the shared SPI bus, writes and reads `/WiFi-LAN-Scanner/scans/a011-wls-sdhil.csv`, deletes only that file, and checks that the panel still answers. An empty socket is `result=absent` and does not fail the suite. A failed readback does. After that probe, the host sends USB Remote frames that start with `@R1 `. Those frames are not HIL commands. `PING` still answers `WLS-HIL PONG`. Production boot also requires `WLS psram-alloc=ok`.

`LIVE` is sent only by `tools/wls_serial.py --mode live`. The host writes the text command, then a length-prefixed SSID and passphrase that are not copied into the transcript. Firmware clears those bytes after `WiFi.begin`, keeps `WiFi.persistent(false)`, and does not call `storeSaved`. The sweep that follows is the production one-step ARP scanner on the derived local subnet, capped at 256 addresses. Silence stays unanswered and is not labeled Offline. The live command refuses a candidate plan that leaves that subnet. After the sweep, the firmware asks link-local mDNS for a reverse hostname of each host the ARP scan already observed. A missing name stays blank. The resolver learned from DHCP is not queried, because it may sit outside the joined subnet. It then classifies each observed MAC from the on-device OUI table. A missing entry stays unknown, and a locally administered MAC stays local.

## Host compiler

PlatformIO's native environment needs a host `g++`. On TF-LAPTOP-00 the user-scoped compiler is WinLibs GCC 16.2.0 (POSIX/UCRT), extracted to `%USERPROFILE%\.local\winlibs\mingw64\bin`. The portable zip SHA-256 is `c1f52294597c0b73786b2a78eb5d176d89226d2f21875eab75e783a8b1cefcc4`. `Invoke-WlsHostTests.ps1` prepends that `bin` directory for the test process when `g++.exe` is there. It does not change the system PATH and does not need administrator rights. An existing `g++` on `PATH` is used when the WinLibs folder is absent.

## What automation proves

The host suite proves the press tracker, face selection, glyph case, masking, password preservation, range math, candidate selection, scanner transitions, synthetic discovery, hostname sanitizing and precedence, host-row detail text, OUI parsing and classification, action parity, CSV escaping, resource-line formatting, and hit/tap/drag behavior. The synthetic HIL run proves those functions execute on the T-Display-S3-Pro, including a fake-backend discovery pass, the fake hostname command, the fixture manufacturer command, the action dispatcher, the in-memory CSV publish, the PSRAM allocation line, and the dedicated SD probe. The HIL serial protocol is present only in the test image. With `-Live`, the same test image then associates to `TFMiddle`, derives the real mask and gateway, runs one capped on-subnet ARP sweep, enriches only the hosts that sweep observed, and writes that inventory under `/WiFi-LAN-Scanner/scans/` when a card is present. It keeps the in-memory rows, exercises USB Remote against them, including a pressed-face acknowledgement for each visible control, and only then runs `LIVECLOSE`. The production boot check proves the restored image still prints the foundation banner, the PSRAM allocation line, and its self-tests, and does not print the HIL ready line. The same script then opens the production USB CDC path, not the HIL router, and requires HELLO, PING, a pressed-face acknowledgement for Find and for Back, the Networks screen, the return to Home, and `WLS touch ready=1`. That production pass does not join Wi-Fi or write NVS.

A person is still required to judge pixel appearance and finger feel. The production image does not embed an automated-test credential. If it has no saved network, it stays disconnected. The live sweep runs only in the test image, and only when `-Live` is requested.

## Adding coverage

Put a new host assertion in `test/test_logic/test_main.cpp` and call the production function directly. Do not copy the logic into the test. Register it with `RUN_TEST`. Keep password checks on a boolean result so a failure does not print the buffer.

A new HIL command belongs inside the `WLS_TEST_MODE` section of `src/HilConsole.cpp`, must reply with the `WLS-HIL` prefix, and must not print secrets or accept an arbitrary string as code. Add the matching expect step in `tools/wls_serial.py`.
