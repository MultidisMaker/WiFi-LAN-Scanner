# WiFi LAN Scanner architecture

This document records the approved component boundaries for the bootstrap. None of the scanning, enrichment, persistence, or touch behavior is implemented yet.

The compile target is PlatformIO board id `lilygo-t-display-s3` (LilyGo T-Display-S3, ESP32-S3). That board id is supplied by the installed PlatformIO Espressif 32 platform. T-Display-S3-Pro peripheral pin mappings, display differences, and any Pro-specific board definition are explicitly deferred. This repository does not invent those mappings.

## Hardware abstraction

Owns board startup, display and touch access, and any later Pro-specific pin or bus mapping. The bootstrap does not select Pro pins. Later work must add a reviewed mapping before the UI or radio code depends on it.

## Wi-Fi manager

Owns association to an operator-supplied network and exposes connection state to the rest of the firmware. The bootstrap does not store SSIDs, passwords, or a credential screen.

## Scanner controller/state machine

Owns the high-level scan lifecycle: idle, scanning, enriching, and presenting results. The bootstrap does not start a scan or define probe timing.

## Discovery engine

Owns active discovery of devices on the joined LAN. Planned later work may include inventory-oriented discovery. The bootstrap does not implement ARP sweeps, ICMP probes, TCP or UDP probes, mDNS, SSDP, or NetBIOS queries. This component is not a passive packet sniffer and is not a vulnerability scanner.

## Enrichment engine

Owns later annotation of discovered devices, including a possible OUI manufacturer lookup. The bootstrap does not ship an OUI database and does not perform lookup.

## Inventory model

Owns the in-memory shape of a discovered device and a scan session. The bootstrap does not define a stored inventory schema and does not record IP or MAC addresses.

## Persistence service

Owns later saving and reloading of inventories on device storage. The bootstrap does not write scan results. Runtime inventories, captures, and generated logs are gitignored so they do not become source artifacts.

## UI/touch layer

Owns the later T-Display-S3-Pro touch interface for starting a scan and reading the inventory. The bootstrap does not draw a UI or bind touch input.

## Build boundary

`platformio.ini` selects the Arduino-ESP32 framework and the family board above. Validation of this baseline is `pio run` only. Firmware upload is out of scope.
