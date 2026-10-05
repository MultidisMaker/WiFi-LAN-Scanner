# Wi-Fi foundation and scan-range limit

## Credential storage

The touchscreen keyboard writes the selected SSID and password into NVS through `Preferences` namespace `wlan`. The password is not printed on serial, not written to the SD card, and not stored in the Git repository.

NVS is plaintext unless flash encryption is enabled. This firmware does not enable flash encryption, and it does not describe the saved password as encrypted.

`WiFi.persistent(false)` is set before connection so the ESP32 Arduino stack does not also store the credential in its default Wi-Fi flash area. Forget Network removes the `ssid` and `psk` keys and disconnects the station.

The automated live test does not use this namespace. It retrieves the `TFMiddle` passphrase from the canonical Agentic credential vault, associates from that transient value, keeps Wi-Fi storage in RAM, and does not call `storeSaved`. The passphrase is not a command-line argument or a repository file. Production firmware has no test passphrase compiled in.

After reboot, a remaining saved SSID is used to reconnect automatically. The password is taken from that NVS entry.

## Range derivation

Given the connected station address and subnet mask:

- prefix length is the count of leading one-bits in a contiguous mask
- network address is address AND mask
- usable host count is `2^(32-prefix) - 2` when the prefix is 1 through 30

Examples checked by `networkRangeSelfTest()`:

| Address | Mask | Prefix | Usable | Future scan count |
|---|---|---|---|---|
| 192.168.0.20 | 255.255.255.0 | 24 | 254 | 254 |
| 10.0.0.5 | 255.255.255.240 | 28 | 14 | 14 |
| 10.1.0.9 | 255.255.0.0 | 16 | 65534 | 256 |

A mask such as `255.0.255.0` is rejected because it is not contiguous. The firmware does not substitute `/24`.

## Scan cap

`kFutureScanHostCap` is 256. `buildCandidatePlan` probes no more than that many eligible on-subnet addresses. Eligible hosts exclude the network address, the broadcast address, and the station. The gateway is included when it is eligible, including when a larger subnet would otherwise push it past the first 256. Silence from a probe is not treated as proof that a host is absent.

## Touch interaction

Find networks, network rows, Back, Previous, Next, Forget, Start, Pause, Resume, Stop, Reset, keyboard keys, Shift, page, backspace, OK, and close use the same press painter. A pressed control is drawn inverted. Shift stays filled, and its label reads `SHIFT`, while uppercase mode is on. Alphabet keys then draw `A-Z`. They draw `a-z` when Shift is off. Shift does not rewrite characters already in the password buffer. The password field stays masked.

## Discovery boundary

Local discovery sends one lwIP ARP request at a time on the directly connected station subnet and reads the ARP cache. It does not send ICMP, TCP, UDP, mDNS, SSDP, or NetBIOS, and it does not capture packets or write an inventory file. A host that does not answer is left out of the list rather than marked offline. Manufacturer text comes from a local OUI table after a MAC is already stored. That lookup does not transmit.
