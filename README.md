# WiFi LAN Scanner

Open-source active LAN inventory and discovery tool for the LILYGO T-Display-S3-Pro.

This firmware is intended to build an on-device inventory of devices on a network the operator has joined. It is not a passive packet sniffer, and it is not a vulnerability scanner.

## Target hardware

- Board family: LILYGO T-Display-S3-Pro
- MCU: ESP32-S3
- Display: 2.33-inch 222x480 touch display

The repository currently contains a PlatformIO Arduino-ESP32 baseline. It compiles with PlatformIO board id `lilygo-t-display-s3`, the existing LilyGo T-Display-S3 family target. T-Display-S3-Pro peripheral pin mappings are deferred. LAN discovery is not implemented yet.

## Build

From this directory, compile only:

```text
pio run -e lilygo-t-display-s3
```

Do not upload or flash firmware from this baseline.

## Layout

`src/`, `include/`, `lib/`, `data/`, `tools/oui/`, `test/`, and `docs/` are the project skeleton. See `docs/architecture.md` for the approved component boundaries.

## License

Released under the MIT License. See [LICENSE](LICENSE).
