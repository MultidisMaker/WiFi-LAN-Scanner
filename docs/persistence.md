# Durable inventory, resource telemetry, and Remote-ready actions

## CSV

Positive observations can be serialized as schema-1 CSV. The intended directory is `/LANScanner/scans/`, for example `/LANScanner/scans/scan-00000001.csv`. Metadata lines start with `#` and record schema, sequence, station, prefix, gateway, candidate count, and cap. The header is:

`ip,mac,method,name,nameSource,macClass,ouiState,manufacturer`

A field that contains a comma, quote, or line break is wrapped in quotes, and an embedded quote is doubled. A missing MAC, name, or manufacturer is empty. Manufacturer text is written only when the OUI state is known. Local, group, unknown, unavailable, none, and unset stay in `macClass` or `ouiState` and are not replaced with a guessed vendor. Unanswered addresses are not rows. The publish helper writes `path.tmp` and renames it onto the final path so a partial write is not the finished file.

The file has no Wi-Fi passphrase, vault material, or packet capture. JSON is not written in this increment. CSV is the format a future Remote client can save.

## SD hardware

The installed GFX Library for Arduino 1.4.6 example `Arduino_GFX_dev_device.h`, branch `LILYGO_T_DISPLAY_S3_PRO`, names the panel pins only. `include/BoardConfig.h` records that mapping and the touch bus from `pro_profile.h` (SDA 5, SCL 6, reset and irq unset). Neither source names an SD chip-select, shared-SPI device, or SDMMC pin. This firmware does not invent one, does not call `SD.begin`, and does not format or erase a card. `storeInventoryOnSd` returns `contract-unproven`. Real SD hardware-in-the-loop was not performed. Host tests and the synthetic `PERSIST` command exercise the serializer in memory.

## Resource telemetry

The diagnostic line is:

`WLS resource phase=<token> heap=<free internal> min=<minimum-ever free> block=<largest free block> psram=<total> freePsram=<free> minPsram=<minimum-ever free>`

Host tests inject the numbers. The device reads the Arduino-ESP32 counters at boot, around a scan, after enrichment, around the persistence attempt, and after reset. Production does not print a sample on every `loop()`.

## Remote-ready seam

Touch input becomes an `AppAction`. `applyAppAction` is the only behavior implementation for start, pause, resume, stop, reset, host navigation, and the Wi-Fi controls that already existed. `AppState` is a read-only snapshot of the current view, Wi-Fi identity without a passphrase, scanner progress, and advisory control flags. A future Remote client should consume that snapshot and those actions rather than framebuffer pixels. No Remote transport, session, or proprietary Remote application is in this repository.

## What each layer proves

Host-native tests prove escaping, repeated serialization, the rename publish, action parity, and the resource line. Synthetic HIL proves those paths run on the T-Display-S3-Pro and that the SD store stays unavailable. Live TFMiddle proves the joined-subnet scan and prints resource samples around that scan. It does not prove a file on an SD card. SD hardware-in-the-loop remains not run.
