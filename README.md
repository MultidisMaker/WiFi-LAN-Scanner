# WiFi LAN Scanner

Open-source active LAN inventory and discovery tool for the LILYGO T-Display-S3-Pro.

This firmware is intended to build an on-device inventory of devices on a network the operator has joined. It is not a passive packet sniffer, and it is not a vulnerability scanner.

## Target hardware

- Board family: LILYGO T-Display-S3-Pro
- MCU: ESP32-S3
- Display: 2.33-inch 222x480 touch display

The current firmware is the Wi-Fi foundation for the LILYGO T-Display-S3-Pro. It provides touchscreen Wi-Fi setup with press-and-release controls, a latched Shift key, NVS-backed saved networks, IPv4 network characterization, and bounded local host discovery on the directly connected subnet. See `docs/architecture.md` and `docs/wifi-foundation.md`.

Saved Wi-Fi credentials are stored in ESP32 NVS. That storage is not encrypted in this firmware.

## Build

From this directory:

```text
pio run -e lilygo-t-display-s3-pro
```

The PlatformIO board id `lilygo-t-display-s3` supplies the ESP32-S3 and 16MB flash target. Pro panel pins come from `include/BoardConfig.h`.

Upload only after the connected USB device has been identified as the intended T-Display-S3-Pro. This repository does not pin an upload port.

```text
pio run -e lilygo-t-display-s3-pro -t upload --upload-port COMx
```

## Automated tests

Host-native regression and the USB serial hardware-in-the-loop path are documented in `docs/testing.md`.

```text
powershell -NoProfile -File tools\Invoke-WlsHostTests.ps1
powershell -NoProfile -File tools\Invoke-WlsRegression.ps1
```

The default regression is host-native tests plus a synthetic hardware-in-the-loop pass. It does not join Wi-Fi. A live pass is separate. `-Live` retrieves the `TFMiddle` passphrase from the canonical Agentic credential vault through `Get-AgenticKeePassCredential.ps1`. The passphrase is not a command-line argument, an environment variable, or a file in this repository.

```text
powershell -NoProfile -File tools\Invoke-WlsRegression.ps1 -Live
```

The test image keeps that passphrase in RAM only and does not write it to the production `wlan` NVS namespace. See `docs/testing.md`.

## Layout

`src/`, `include/`, `lib/`, `data/`, `tools/oui/`, `test/`, and `docs/` are the project layout. Hosts already found by the bounded ARP scan can receive a link-local mDNS hostname. OUI enrichment, service enumeration, and inventory persistence are still deferred. `tools/oui/` is not used by this firmware.

## Community

- GitHub organization: [MultidisMaker](https://github.com/MultidisMaker)
- This repository: [WiFi-LAN-Scanner](https://github.com/MultidisMaker/WiFi-LAN-Scanner)
- YouTube: [MultidisMaker](https://www.youtube.com/@MultidisMaker)
- Patreon: [MultidisMaker](https://www.patreon.com/cw/MultidisMaker)

## License

Released under the MIT License. See [LICENSE](LICENSE).
