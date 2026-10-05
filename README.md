# WiFi LAN Scanner

Open-source active LAN inventory and discovery tool for the LILYGO T-Display-S3-Pro.

This firmware is intended to build an on-device inventory of devices on a network the operator has joined. It is not a passive packet sniffer, and it is not a vulnerability scanner.

## Target hardware

- Board family: LILYGO T-Display-S3-Pro
- MCU: ESP32-S3
- Display: 2.33-inch 222x480 touch display

The current firmware is the first functional increment for the LILYGO T-Display-S3-Pro. It provides touchscreen Wi-Fi setup, NVS-backed saved networks, IPv4 network characterization, and a scanner-controller state machine. Host discovery is not implemented. See `docs/architecture.md` and `docs/wifi-foundation.md`.

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

## Layout

`src/`, `include/`, `lib/`, `data/`, `tools/oui/`, `test/`, and `docs/` remain the project skeleton. Discovery, enrichment, and inventory persistence are still deferred.

## License

Released under the MIT License. See [LICENSE](LICENSE).
