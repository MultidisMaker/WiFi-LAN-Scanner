# WiFi LAN Scanner architecture

This firmware implements the hardware, Wi-Fi, UI, network-characterization, scanner-controller, bounded local host-discovery path, link-local hostname enrichment, offline manufacturer enrichment, schema-1 CSV persistence for hosts that discovery already found, and USB Remote protocol v1 on that same action seam. The scanner stores a Service Scan profile and can show it. TCP and UDP service probes are not implemented. Wi-Fi Remote transport remains deferred. See `docs/service-scan.md`.

## Hardware abstraction

`include/BoardConfig.h` and `src/DisplayBoard.cpp` use the installed GFX Library for Arduino 1.4.6 definition `LILYGO_T_DISPLAY_S3_PRO`:

- Panel: ST7796, 222x480, IPS, rotation 0, column offset 49
- SPI: DC 9, CS 39, SCK 18, MOSI 17, MISO 8, RST 47
- Backlight: GPIO 48, driven with LEDC as on the TF-LAPTOP-00 diagnostic that already exercised this panel

Source file: `Arduino_GFX_dev_device.h` in GFX Library for Arduino 1.4.6, branch `LILYGO_T_DISPLAY_S3_PRO`.

Touch uses SensorLib 0.1.6 `TouchDrvCSTXXX`, vendored at `lib/SensorLib` from the installed 0.1.6 tree, at CST226SE address `0x5A` on SDA 5 / SCL 6. Reset and interrupt stay at -1. Those I2C pins and the unassigned reset/irq pins are the observed Pro touch path recorded by the local diagnostic `Documents\MakerNexus\GarageController\Firmware\src\pro_profile.h`. The non-Pro 170x320 parallel panel pins are not used.

The PlatformIO board id remains `lilygo-t-display-s3` because that is the installed ESP32-S3 / 16MB-flash target. Its `memory_type` is `qio_opi`. This project repeats `board_build.arduino.memory_type = qio_opi` and defines `BOARD_HAS_PSRAM`. Without that macro the Arduino core undefines `CONFIG_SPIRAM`, and `ESP.getPsramSize()` stays 0 even on an ESP32-S3R8 module. The Pro panel map still comes from `BoardConfig.h`, not from the non-Pro variant header.

The onboard SD socket uses the display SPI bus. LilyGO `examples/factory/utilities.h` names MISO 8, MOSI 17, SCK 18, TFT CS 39, and SD CS 14. Firmware holds GPIO 14 high before the panel starts, passes those same clock and data pins to `SPI.begin` when a save or SD probe needs the bus, and calls `SD.begin` with `format_if_empty=false`. Camera, PMIC, ambient light, and buttons are not initialized. Touch reset and interrupt stay at -1.

## Wi-Fi manager

`src/WifiService.cpp` scans nearby access points, shows SSID, RSSI, and open/secured state, accepts an on-device password, connects, forgets the saved network, and reconnects after reboot when a saved network remains.

Saved credentials go to ESP32 Preferences namespace `wlan` under keys `ssid` and `psk`. `WiFi.persistent(false)` keeps the Arduino Wi-Fi stack from making a second SDK copy. This NVS storage is not encrypted. Flash encryption is not enabled. The firmware does not print passwords or write them to source, serial logs, or files.

## Network characterization

`src/NetworkRange.cpp` reads the station IPv4 address, subnet mask, gateway, and DNS servers from the Arduino-ESP32 `WiFi` object and passes them to `deriveNetFacts` in `src/NetMath.cpp`. That shared arithmetic is what host tests exercise. The network address and prefix come from the address AND the mask. Usable host count is `2^(32-prefix) - 2` for a contiguous prefix from 1 through 30. The code does not assume `/24`.

A non-contiguous mask, or a prefix outside 1..30, is reported as an unavailable range. The scanner does not assume `/24`. It refuses to start when Wi-Fi is disconnected or the range is invalid.

`buildCandidatePlan` in `src/CandidatePlan.cpp` selects who may be probed. Eligible addresses are the usable hosts, excluding the network address, the broadcast address, and the station itself. The gateway is eligible when it is one of those hosts. The plan never walks a huge subnet: it keeps at most 256 addresses (`kFutureScanHostCap`). When the usable subnet fits in the batch, Automatic keeps the lowest addresses. When it does not, Automatic keeps the count-aligned block around the station, clipped to the real mask, and adds an outside gateway only if the batch still has room. The list is sorted ascending. A custom start stays inside the same joined subnet, still at most 256 addresses, and is not reused after the joined network changes. Only the batch size, 64, 128, or 256, is stored. See `docs/address-range.md`.

## Local discovery

The device sends at most one lwIP ARP request at a time, then reads `etharp_find_addr` on the station netif. Those are the stock lwIP functions declared in the installed Arduino-ESP32 2.x header `lwip/etharp.h` (ESP32-S3 SDK under PlatformIO `framework-arduinoespressif32`). The installed lwIP default `ARP_TABLE_SIZE` is 10, so requests are not pipelined. Each probe waits up to 200 ms. An address outside the station subnet, or the station's own address, is not transmitted. A missing reply is recorded as unanswered and is not shown as Offline. A MAC address is stored only when the ARP cache returns one. The ARP scanner does not send TCP, UDP, ICMP, SSDP, NetBIOS, or probes beyond the directly connected subnet.

After a host is observed, `src/MdnsEnricher.cpp` may ask mDNS for that host's reverse name. The query is one PTR for `{d}.{c}.{b}.{a}.in-addr.arpa` with no service type and no protocol, so it is not a service browse. One query is in flight, and each attempt is capped at about 400 ms. A missing answer leaves the name blank and keeps the IP and MAC. The firmware does not send reverse DNS to the DHCP resolver, because that resolver can be outside the joined subnet. Names are sanitized to at most 31 characters. An mDNS name outranks a reverse-DNS name, and the same source keeps the lexicographically smaller sanitized name. The host row shows the IPv4 address and a detail line with a one-letter source tag, the display name or `unknown`, and the MAC or `MAC unknown`. A third line shows the manufacturer when the offline OUI table has one.

Three layers cover this path. Host-native tests use `FakeDiscoveryBackend` and do not transmit. The synthetic HIL image uses that same fake backend over the serial protocol and checks fake names without joining Wi-Fi. The optional live HIL command associates with a vault-supplied transient credential and then uses the production `LwipArpBackend` plus the mDNS reverse lookup on the joined subnet only. The HIL serial protocol, including the live command, exists only in the test image. The production image uses `LwipArpBackend` and the mDNS enricher, and it has no test passphrase.

Observed hosts stay in RAM for the current boot. They are de-duplicated by IPv4 address and, when a MAC is present, by that MAC. A later weaker observation does not erase a MAC already learned. Reset and a new scan clear the list. Nothing is written to NVS, SD, or a file.

## Manufacturer enrichment

`src/Oui.cpp` classifies a stored MAC, then may attach a manufacturer from a local table. The group bit is checked first, including broadcast and a MAC that also has the local bit set. A locally administered unicast MAC is labeled local and never receives a guessed vendor. A globally administered prefix that is absent from the table stays unknown. An empty table leaves the host marked unavailable and does not clear its IP, MAC, or hostname.

The table is produced by `tools/oui/build_oui_index.py` from the public IEEE MA-L CSV `https://standards-oui.ieee.org/oui/oui.csv`. The script keeps MA-L rows, accepts `AABBCC`, `AA:BB:CC`, and `AA-BB-CC`, sanitizes organization names to at most 64 characters, and keeps the lexicographically smaller name when an assignment is repeated. Non-ASCII characters are dropped. One published row, assignment `04208A`, has an organization name that contains no retained characters, so that prefix stays unknown rather than being transliterated. The raw CSV is not committed. Refresh instructions are in `tools/oui/README.md`.

Device builds set `WLS_OUI_EMBEDDED` and compile `src/OuiData.gen.inc` into flash. The enricher binary-searches that table and updates at most 32 not-yet-classified hosts per `loop()` call. Host tests use a small fixture instead of the generated registry, and the native image leaves the embedded table empty. The host-list row is 48 pixels tall: the IPv4 address, the existing name and MAC line, and a clipped manufacturer line. The 34-pixel home card keeps the address and name/MAC line so the button positions stay put. The full stored name remains on the host record.

## Scanner controller

`src/ScannerController.cpp` has `IDLE`, `STARTING`, `SCANNING`, `PAUSED`, `STOPPING`, and `COMPLETE`. `STARTING` and `STOPPING` advance on a 200 ms timer inside `loop()`. `SCANNING` services one probe step per `loop()` call, so the touch UI keeps running. Pause does not start or finish another candidate. Stop keeps the hosts already observed. Reset returns to a clean idle list.

## UI / touch

`src/ScannerUi.cpp` is a 222x480 portrait layout: Wi-Fi status, network list, on-device keyboard, network facts, scanner controls, and a Settings screen. The home title is `WiFi-LAN-Scanner`. Hosts, Networks, Password, and Settings keep short screen names. Password glyphs on screen are asterisks. Home repaints dirty bands through one 222x160 PSRAM sprite instead of clearing the whole panel on a timer. Pause is shown while the scan is `SCANNING`, and Resume is shown while it is `PAUSED`. Stop stays available while a scan can be stopped. Reset is present when the scanner is idle or complete and uses a dimmer face so it does not compete with Start.

Actionable controls share one geometry list (`collectUiControls` in `src/UiModel.cpp`), one painter, and one press tracker (`include/UiPress.h`). Host tests and the test-only HIL build call that same list. Touch-down paints that control immediately. Release inside the same control runs its action once. Sliding off before release cancels the action and restores the normal face. A tap shorter than 120 ms keeps the pressed face until 120 ms from touch-down so the acknowledgement stays visible. A USB Remote action that matches a control on the current screen paints that same pressed face for 120 ms before the action runs. The timer stays inside `loop()` and does not stall USB or the scan. A second action during that interval is rejected as busy. The Shift control stays filled while uppercase mode is on, and alphabet labels are an explicit `a-z`/`A-Z` map. `toupper` is not used. Serial logs on the password screen omit coordinates and key identity. See `docs/testing.md`.

## Inventory export

`include/InventoryExport.h` writes schema-1 CSV for hosts the scan already observed. The directory is `/WiFi-LAN-Scanner/scans/`. A publish writes `path.tmp` and renames it to the final name. Unanswered addresses are omitted. Local, group, unknown, and unavailable manufacturer states stay explicit, and a label is stored only for a known global assignment. The CSV has no passphrase column. See `docs/persistence.md`.

`storeInventoryOnSd` streams that CSV to `/WiFi-LAN-Scanner/scans/scan-########.csv` on the device. The host build has no card and still returns `contract-unproven`. A missing card stays `media-absent` and does not format the socket. The synthetic `PERSIST` command checks the RAM serializer only and does not mount the card. `SDPROBE` on the test image does. An older `/LANScanner/scans/` directory is left in place when it is present.

## Resource telemetry

`src/ResourceFormat.cpp` formats one line from injected counters. On the device, `src/ResourceMeter.cpp` fills that line from `ESP.getFreeHeap`, `getMinFreeHeap`, `getMaxAllocHeap`, `getPsramSize`, `getFreePsram`, and `getMinFreePsram`. Production prints it at ready, once when a scan starts, once during the scan, after completion, after name and manufacturer enrichment go idle, before and after the persistence attempt, and after reset. It is not printed on every `loop()`.

## Remote-ready actions

`include/AppActions.h` is the only place a touch control or a USB Remote action changes the scanner or the host-list view. `ScannerUi::dispatch` translates a control id into an `AppAction` and calls `applyAppAction`. USB Remote protocol v1 calls that same function. Host tests call it directly. `fillAppState` copies the authoritative scanner and view into a passphrase-free snapshot. Remote `STATE` is that snapshot. It does not read the framebuffer. The framed USB transport is documented in `docs/remote-v1.md`. Wi-Fi transport, TLS, pairing, and a proprietary Remote application are not in this repository.

Host-row control ids are 200 through 205. Previous, Next, and Back keep their own ids.

## Still deferred

- ICMP, TCP, UDP, mDNS service browse, SSDP, and NetBIOS
- JSON inventory export
- Wi-Fi Remote transport, TLS, pairing, and the proprietary Remote application
- T-Display-S3-Pro pins that this firmware does not use
- A technician-entered saved network is still required before the production UI can scan; the automated live check is test-image only
