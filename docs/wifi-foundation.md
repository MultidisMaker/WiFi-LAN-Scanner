# Wi-Fi foundation and scan-range limit

## Credential storage

The touchscreen keyboard writes the selected SSID and password into NVS through `Preferences` namespace `wlan`. The password is not printed on serial, not written to the SD card, and not stored in the Git repository.

NVS is plaintext unless flash encryption is enabled. This firmware does not enable flash encryption, and it does not describe the saved password as encrypted.

`WiFi.persistent(false)` is set before connection so the ESP32 Arduino stack does not also store the credential in its default Wi-Fi flash area. Forget Network removes the `ssid` and `psk` keys and disconnects the station.

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

## Future scan cap

`kFutureScanHostCap` is 256. A later discovery increment must not probe more than the smaller of the usable host count and this cap. The current scanner state machine does not probe hosts at all.

## Deferred discovery

No ARP sweep, ICMP echo, TCP or UDP probe, mDNS, SSDP, NetBIOS, OUI lookup, packet capture, or inventory file is implemented.
