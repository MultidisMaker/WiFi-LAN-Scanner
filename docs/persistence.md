# Durable inventory, resource telemetry, and Remote-ready actions

## CSV

Positive observations can be serialized as schema-1 CSV. The directory is `/WiFi-LAN-Scanner/scans/`, for example `/WiFi-LAN-Scanner/scans/scan-00000001.csv`. Metadata lines start with `#` and record schema, sequence, station, prefix, gateway, candidate count, and cap. The header is:

`ip,mac,method,name,nameSource,macClass,ouiState,manufacturer`

A field that contains a comma, quote, or line break is wrapped in quotes, and an embedded quote is doubled. A missing MAC, name, or manufacturer is empty. Manufacturer text is written only when the OUI state is known. Local, group, unknown, unavailable, none, and unset stay in `macClass` or `ouiState` and are not replaced with a guessed vendor. Unanswered addresses are not rows. The publish helper writes `path.tmp` and renames it onto the final path so a partial write is not the finished file.

The file has no Wi-Fi passphrase, vault material, or packet capture. The card file stays schema-1 CSV. USB Remote reads the same in-memory rows and does not reread this file.

## SD hardware

LilyGO T-Display-S3-Pro `examples/factory/utilities.h` says the SD socket and the TFT share one SPI bus: MISO GPIO 8, MOSI GPIO 17, SCK GPIO 18, TFT chip-select GPIO 39, SD chip-select GPIO 14. Those display pins are the ones this firmware already used. `SD.begin` is called with the SD chip-select, that SPI object, 4 MHz, mount point `/sd`, and `format_if_empty=false`. The firmware never formats, repartitions, or erases the card. GPIO 14 is held high before the panel is started and both chip-selects are left high after an SD transaction.

The card path is `/WiFi-LAN-Scanner/scans/scan-########.csv`. The writer creates those directories when they are missing, writes `path.tmp`, and renames it onto a sequence name that is not already present. Rows are written one at a time. A missing card is reported as `media-absent` and is not retried for the rest of that boot. The host build of `storeInventoryOnSd` still returns `contract-unproven`. The synthetic `PERSIST` command stays in RAM and prints `sd=skipped`. The test image's `SDPROBE` command writes, reads back, and deletes only `/WiFi-LAN-Scanner/scans/a011-wls-sdhil.csv`. A legacy `/LANScanner/scans/` directory is reported and left untouched.

## Resource telemetry

The diagnostic line is:

`WLS resource phase=<token> heap=<free internal> min=<minimum-ever free> block=<largest free block> psram=<total> freePsram=<free> minPsram=<minimum-ever free>`

Host tests inject the numbers. The device reads the Arduino-ESP32 counters at boot, around a scan, after enrichment, around the persistence attempt, and after reset. Production does not print a sample on every `loop()`.

## Remote-ready seam

Touch input and USB Remote protocol v1 become an `AppAction`. `applyAppAction` is the only behavior implementation for start, pause, resume, stop, reset, host navigation, and the Wi-Fi controls that already existed. `AppState` is a read-only snapshot of the current view, Wi-Fi identity without a passphrase, scanner progress, and advisory control flags. Remote `GET_STATE` returns that snapshot. See `docs/remote-v1.md`. Wi-Fi transport and a proprietary Remote application are not in this repository.

## What each layer proves

Host-native tests prove escaping, repeated serialization, the rename publish, action parity, and the resource line. Synthetic HIL proves those paths run on the T-Display-S3-Pro. `SDPROBE` then proves the shared-bus card path when a card is inserted, or reports the socket empty without failing the serializer. Live TFMiddle proves the joined-subnet scan, prints resource samples around that scan, and uses the same production writer.
