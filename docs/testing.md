# Testing

Host-native tests and a test-only serial HIL mode cover the shared firmware logic. The normal production image does not include the HIL protocol. Host discovery is still not implemented.

## Shared logic

These files are compiled into both the firmware and the host tests:

- `src/UiPress.cpp` — press, release, drag-off, acknowledgement, faces, glyphs, and masking
- `src/NetMath.cpp` — IPv4 prefix, usable-host count, and the 256-host future cap
- `src/PasswordBuffer.cpp` — typed password state, including Shift preservation
- `src/ScannerController.cpp` — scanner state machine
- `src/UiModel.cpp` — control geometry and synthetic gestures

`src/ScanClock.cpp` supplies `scanNow()` from `millis()` on the device. Host tests supply their own `scanNow()` so transitions can be stepped without waiting. `src/NetworkRange.cpp` and `src/ScannerUi.cpp` adapt the shared helpers to Arduino types and the panel. They are not part of the host build.

## Commands

From the repository root:

```text
powershell -NoProfile -File tools\Invoke-WlsHostTests.ps1
powershell -NoProfile -File tools\Invoke-WlsRegression.ps1
```

The host command runs `pio test -e native`. The regression command runs that suite, builds the production image, identifies the connected board, uploads the HIL image, runs the serial script, then uploads the production image again and checks its boot log. It returns a non-zero exit code if any required step fails. If the HIL image was uploaded, the script still attempts to restore production firmware.

`tools/Invoke-WlsRegression.ps1` accepts `-WorkDir` for verbose logs and `-EvidenceDir` for the short transcripts. Neither path is committed.

## Production and test builds

| Environment | Command | HIL protocol |
| --- | --- | --- |
| `lilygo-t-display-s3-pro` | `pio run -e lilygo-t-display-s3-pro` | absent (`WLS_TEST_MODE` is 0) |
| `lilygo-t-display-s3-pro-hil` | `pio run -e lilygo-t-display-s3-pro-hil` | present (`-DWLS_TEST_MODE=1`) |
| `native` | `pio test -e native` | not built |

The HIL environment extends the production environment and adds only the test-mode macro. `platformio.ini` does not set `upload_port`. The regression script selects the single Espressif USB serial device whose parent id is `VID_303A&PID_1001` and USB serial `80:65:99:A0:3E:70`, then checks `flash_id` for that MAC and a 16MB flash. Zero or multiple matches stop the run.

The production `.bin` and `.elf` must not contain the ASCII token `WLS-HIL`. The HIL image must contain it. HIL replies use that prefix. The commands are `PING`, `SELF`, `UI`, `TAP`, `DRAG`, `KEYS`, `PRESERVE`, and `SCAN`. They do not join Wi-Fi, start a network scan, print passwords, or offer a general shell.

## Host compiler

PlatformIO's native environment needs a host `g++`. On TF-LAPTOP-00 the user-scoped compiler is WinLibs GCC 16.2.0 (POSIX/UCRT), extracted to `%USERPROFILE%\.local\winlibs\mingw64\bin`. The portable zip SHA-256 is `c1f52294597c0b73786b2a78eb5d176d89226d2f21875eab75e783a8b1cefcc4`. `Invoke-WlsHostTests.ps1` prepends that `bin` directory for the test process when `g++.exe` is there. It does not change the system PATH and does not need administrator rights. An existing `g++` on `PATH` is used when the WinLibs folder is absent.

## What automation proves

The host suite proves the press tracker, face selection, glyph case, masking, password preservation, range math, scanner transitions, and synthetic hit/tap/drag behavior. The HIL run proves those same functions execute on the T-Display-S3-Pro and that the serial protocol is present only in the test image. The production boot check proves the restored image still prints the foundation banner and its self-tests, and does not print the HIL ready line.

A person is still required to judge pixel appearance, finger feel, and a real Wi-Fi association. This phase does not connect to Wi-Fi and does not read saved credentials.

## Adding coverage

Put a new host assertion in `test/test_logic/test_main.cpp` and call the production function directly. Do not copy the logic into the test. Register it with `RUN_TEST`. Keep password checks on a boolean result so a failure does not print the buffer.

A new HIL command belongs inside the `WLS_TEST_MODE` section of `src/HilConsole.cpp`, must reply with the `WLS-HIL` prefix, and must not print secrets or accept an arbitrary string as code. Add the matching expect step in `tools/wls_serial.py`.
