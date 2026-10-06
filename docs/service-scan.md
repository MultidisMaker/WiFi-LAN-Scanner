# Service Scan design contract

A013 stores a Service Scan profile and shows it on the scanner. It does not open TCP or UDP sockets, send banners, or issue HTTP requests. The scanner remains the authority. A future Remote client reads the selected profile from `AppState` and changes it with the same actions the touchscreen uses.

## Profiles

The setting lives in `ServiceProfile`. A missing or unrecognized value resolves to Common. The device stores the token in Preferences namespace `wls`, key `profile`, which is separate from the Wi-Fi passphrase namespace `wlan`. Tokens are `basic`, `common`, and `detailed`.

| Profile | Label | Intent | Proposed TCP attempts per ARP-observed host | Per-attempt timeout |
| --- | --- | --- | --- | --- |
| Basic | Basic / fast | Short pass | 3 | 200 ms, no retry |
| Common | Common / recommended | Default | 9 | 250 ms, no retry |
| Detailed | Detailed / slower | Deeper curated set | 20 | 300 ms, no retry |

The T-Display does not expose raw timeout knobs.

## Where probes are allowed to run later

A later increment may connect only to hosts already present in the ARP-observed inventory. Silence on ARP is not a target. The attempt budget is `observed_host_count * profile_port_count`. It is not `candidate_count * profile_port_count`. The 256-address candidate cap stays an ARP limit and is not multiplied by the port list.

One TCP connect is in flight at a time, matching the one-ARP rule. Pause and stop use the existing scanner states. A per-host ceiling is the attempt count times that profile's timeout. There is no retry.

A completed handshake is Open. A reset is Closed. Any other miss is Timeout-or-Unknown. The classification does not use credentials, banners, HTTP requests, or service commands.

## Proposed port families

These ports are a design table only. A013 does not transmit to them.

Basic:

- 22 SSH
- 80 HTTP
- 443 HTTPS

Common adds:

- 445 direct-host SMB
- 548 AFP
- 631 IPP
- 8080 HTTP alternate
- 8443 HTTPS alternate
- 9100 raw printing

Detailed adds:

- 21 FTP
- 23 Telnet
- 25 SMTP
- 53 DNS over TCP
- 110 POP3
- 143 IMAP
- 587 mail submission
- 993 IMAPS
- 995 POP3S
- 1883 MQTT
- 8883 MQTTS

The families are ordinary service identifiers for a later inventory label. They are not a vulnerability scan.

## Order

The intended later pipeline is ARP discovery, then the existing name and OUI enrichment, then Service Scan against ARP-observed hosts only, then inventory enrichment of those same hosts. Unanswered candidate addresses stay out of that last stage.
