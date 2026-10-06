# Service Scan design contract

The scanner stores a Service Scan profile and, after ARP discovery and name enrichment, opens one TCP connection at a time to MAC-bearing hosts already in that inventory. It does not send banners, HTTP, UDP, or credentials. The scanner remains the authority. Remote reads the selected profile from `AppState` and changes it with the same actions the touchscreen uses.

## Profiles

The setting lives in `ServiceProfile`. A missing or unrecognized value resolves to Common. The device stores the token in Preferences namespace `wls`, key `profile`, which is separate from the Wi-Fi passphrase namespace `wlan`. Tokens are `basic`, `common`, and `detailed`.

| Profile | Label | Intent | Proposed TCP attempts per ARP-observed host | Per-attempt timeout |
| --- | --- | --- | --- | --- |
| Basic | Basic / fast | Short pass | 3 | 200 ms, no retry |
| Common | Common / recommended | Default | 9 | 250 ms, no retry |
| Detailed | Detailed / slower | Deeper curated set | 20 | 300 ms, no retry |

The T-Display does not expose raw timeout knobs.

## Where probes run

Service Scan connects only to hosts already present in the ARP-observed inventory that have a MAC. Silence on ARP is not a target. The attempt budget is `observed_mac_host_count * profile_port_count`. It is not `candidate_count * profile_port_count`. The 256-address candidate cap stays an ARP limit and is not multiplied by the port list.

One nonblocking TCP connect is in flight at a time. The socket is closed as soon as the attempt is classified, and nothing is written or read. Pause and stop use the existing scanner controls. A new probe is not started while paused. Stop closes the active socket and does not start another. Reset clears the service results with the inventory. There is no retry.

A completed handshake is Open (`o`). An active reject is Closed (`c`): on this ESP32 lwIP build that is `ECONNREFUSED`, `ECONNRESET`, or `ECONNABORTED`. A miss that reaches the profile timeout, or an unreachable host, is Timeout (`t`). Any other socket failure is Error (`e`). Timeout is not stored as closed. The classification does not use credentials, banners, HTTP requests, or service commands.

The connect runs only after the scanner is Complete and PTR/OUI enrichment is idle. It does not return the scanner to Scanning, because that state is what starts discovery and clears the PTR cycle.

## Port families

These are the ordered TCP lists. Basic is the first three, Common is the first nine, and Detailed is all twenty. The family name is an inventory label, not a vulnerability check.

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
