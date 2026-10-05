# WiFi LAN Scanner architecture

This increment implements the hardware, Wi-Fi, UI, network-characterization, and scanner-controller foundation. The discovery engine, enrichment engine, inventory model, and inventory persistence service are still deferred. No host-discovery probes are sent.

## Hardware abstraction

`include/BoardConfig.h` and `src/DisplayBoard.cpp` use the installed GFX Library for Arduino 1.4.6 definition `LILYGO_T_DISPLAY_S3_PRO`:

- Panel: ST7796, 222x480, IPS, rotation 0, column offset 49
- SPI: DC 9, CS 39, SCK 18, MOSI 17, MISO 8, RST 47
- Backlight: GPIO 48, driven with LEDC as on the TF-LAPTOP-00 diagnostic that already exercised this panel

Source file: `Arduino_GFX_dev_device.h` in GFX Library for Arduino 1.4.6, branch `LILYGO_T_DISPLAY_S3_PRO`.

Touch uses SensorLib 0.1.6 `TouchDrvCSTXXX`, vendored at `lib/SensorLib` from the installed 0.1.6 tree, at CST226SE address `0x5A` on SDA 5 / SCL 6. Reset and interrupt stay at -1. Those I2C pins and the unassigned reset/irq pins are the observed Pro touch path recorded by the local diagnostic `Documents\MakerNexus\GarageController\Firmware\src\pro_profile.h`. The non-Pro 170x320 parallel panel pins are not used.

The PlatformIO board id remains `lilygo-t-display-s3` because that is the installed ESP32-S3 / 16MB-flash target. It does not supply the Pro panel map.

Camera, PMIC, ambient light, SD, and buttons are not initialized. No installed Pro source used here names a touch reset, touch interrupt, or button GPIO, so none is guessed.

## Wi-Fi manager

`src/WifiService.cpp` scans nearby access points, shows SSID, RSSI, and open/secured state, accepts an on-device password, connects, forgets the saved network, and reconnects after reboot when a saved network remains.

Saved credentials go to ESP32 Preferences namespace `wlan` under keys `ssid` and `psk`. `WiFi.persistent(false)` keeps the Arduino Wi-Fi stack from making a second SDK copy. This NVS storage is not encrypted. Flash encryption is not enabled. The firmware does not print passwords or write them to source, serial logs, or files.

## Network characterization

`src/NetworkRange.cpp` reads the station IPv4 address, subnet mask, gateway, and DNS servers from the Arduino-ESP32 `WiFi` object. The network address and prefix come from the address AND the mask. Usable host count is `2^(32-prefix) - 2` for a contiguous prefix from 1 through 30. The code does not assume `/24`.

A non-contiguous mask, or a prefix outside 1..30, is reported as an unavailable range. A future discovery pass may examine at most 256 usable hosts (`kFutureScanHostCap`), even if the subnet is larger. This increment does not send those probes. The UI shows the derived range and the cap.

## Scanner controller

`src/ScannerController.cpp` has `IDLE`, `STARTING`, `SCANNING`, `PAUSED`, `STOPPING`, and `COMPLETE`. `STARTING` and `STOPPING` advance on a 200ms timer inside `loop()`. `SCANNING` does not transmit. The touch UI keeps running while states change.

## UI / touch

`src/ScannerUi.cpp` is a 222x480 portrait layout: Wi-Fi status, network list, on-device keyboard, network facts, and scanner controls. Password glyphs on screen are asterisks.

Actionable controls share one geometry list, one painter, and one press tracker (`include/UiPress.h`). Touch-down paints that control immediately. Release inside the same control runs its action once. Sliding off before release cancels the action and restores the normal face. A tap shorter than 120 ms keeps the pressed face until 120 ms from touch-down so the acknowledgement stays visible. The Shift control stays filled while uppercase mode is on, and alphabet labels are an explicit `a-z`/`A-Z` map. `toupper` is not used. Serial logs on the password screen omit coordinates and key identity.

## Still deferred

- Discovery engine: ARP, ICMP, TCP, UDP, mDNS, SSDP, and NetBIOS
- Enrichment engine and OUI lookup
- Inventory model and persistence
- SD-card scan history
- T-Display-S3-Pro pins that this increment does not use
