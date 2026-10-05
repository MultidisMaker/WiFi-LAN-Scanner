# WiFi LAN Scanner architecture

This firmware implements the hardware, Wi-Fi, UI, network-characterization, scanner-controller, bounded local host-discovery path, link-local hostname enrichment, and offline manufacturer enrichment for hosts that discovery already found. Positive observations can be serialized as CSV. The SD card is not mounted. Service enumeration and a Remote transport remain deferred.

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

`src/NetworkRange.cpp` reads the station IPv4 address, subnet mask, gateway, and DNS servers from the Arduino-ESP32 `WiFi` object and passes them to `deriveNetFacts` in `src/NetMath.cpp`. That shared arithmetic is what host tests exercise. The network address and prefix come from the address AND the mask. Usable host count is `2^(32-prefix) - 2` for a contiguous prefix from 1 through 30. The code does not assume `/24`.

A non-contiguous mask, or a prefix outside 1..30, is reported as an unavailable range. The scanner does not assume `/24`. It refuses to start when Wi-Fi is disconnected or the range is invalid.

`buildCandidatePlan` in `src/CandidatePlan.cpp` selects who may be probed. Eligible addresses are the usable hosts, excluding the network address, the broadcast address, and the station itself. The gateway is eligible when it is one of those hosts. The plan never walks a huge subnet: it keeps at most 256 addresses (`kFutureScanHostCap`). When more hosts are eligible, it keeps the 256 lowest. If the gateway is eligible and lies above that window, the gateway replaces the highest selected address and the list is sorted ascending again.

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

`src/ScannerUi.cpp` is a 222x480 portrait layout: Wi-Fi status, network list, on-device keyboard, network facts, and scanner controls. Password glyphs on screen are asterisks.

Actionable controls share one geometry list (`collectUiControls` in `src/UiModel.cpp`), one painter, and one press tracker (`include/UiPress.h`). Host tests and the test-only HIL build call that same list. Touch-down paints that control immediately. Release inside the same control runs its action once. Sliding off before release cancels the action and restores the normal face. A tap shorter than 120 ms keeps the pressed face until 120 ms from touch-down so the acknowledgement stays visible. The Shift control stays filled while uppercase mode is on, and alphabet labels are an explicit `a-z`/`A-Z` map. `toupper` is not used. Serial logs on the password screen omit coordinates and key identity. See `docs/testing.md`.

## Inventory export

`include/InventoryExport.h` writes schema-1 CSV for hosts the scan already observed. The directory is `/LANScanner/scans/`. A publish writes `path.tmp` and renames it to the final name. Unanswered addresses are omitted. Local, group, unknown, and unavailable manufacturer states stay explicit, and a label is stored only for a known global assignment. The CSV has no passphrase column. See `docs/persistence.md`.

`storeInventoryOnSd` returns unavailable. The installed GFX `LILYGO_T_DISPLAY_S3_PRO` example and the touch profile do not name an SD chip-select or SDMMC bus, so the firmware does not call `SD.begin` and does not choose a pin.

## Resource telemetry

`src/ResourceFormat.cpp` formats one line from injected counters. On the device, `src/ResourceMeter.cpp` fills that line from `ESP.getFreeHeap`, `getMinFreeHeap`, `getMaxAllocHeap`, `getPsramSize`, `getFreePsram`, and `getMinFreePsram`. Production prints it at ready, once when a scan starts, once during the scan, after completion, after name and manufacturer enrichment go idle, before and after the persistence attempt, and after reset. It is not printed on every `loop()`.

## Remote-ready actions

`include/AppActions.h` is the only place a touch control changes the scanner or the host-list view. `ScannerUi::dispatch` translates a control id into an `AppAction` and calls `applyAppAction`. Host tests call that same function directly. `fillAppState` copies the authoritative scanner and view into a passphrase-free snapshot. A future Remote client should send these actions and read this snapshot. It should not read the framebuffer. No USB, Wi-Fi, TLS, pairing, or Remote application is implemented, and that application is not in this repository.

Host-row control ids are 200 through 205. Previous, Next, and Back keep their own ids.

## Still deferred

- ICMP, TCP, UDP, mDNS service browse, SSDP, and NetBIOS
- SD card mount, until an authoritative T-Display-S3-Pro SD pin and bus contract is known
- JSON inventory export
- Wi-Fi or USB Remote transport and the proprietary Remote application
- T-Display-S3-Pro pins that this firmware does not use
- A technician-entered saved network is still required before the production UI can scan; the automated live check is test-image only
