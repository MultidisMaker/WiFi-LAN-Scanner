# USB Remote protocol v1

Remote Protocol v1 is a framed command channel on the device's USB serial port. It is not a Wi-Fi service. This repository does not contain a desktop or mobile Remote application.

## Framing

A frame is the four characters `@R1 `, one flat JSON object, and a newline. The characters before the newline, including the prefix, are at most 320 bytes. Setup raises the USB CDC receive and transmit queues from the 256-byte default to 512 bytes before `Serial.begin`, so one maximum frame and its newline fit. The object contains only strings and integers. Nested objects, arrays, and booleans are rejected. Quotes and backslashes in strings are escaped. Control characters are omitted on output and rejected on input.

Diagnostic lines start with `WLS ` or `WLS-HIL ` and are not frames. HIL commands do not use the `@R1 ` prefix. The text command `PING` answers `WLS-HIL PONG`. A remote ping is a frame such as `@R1 {"v":1,"op":"PING","id":1}` and answers `@R1 {"v":1,"op":"PONG","id":1}`.

Production firmware reads the USB port for frames. The test image keeps one reader: HIL lines stay on the HIL parser, and only `@R1 ` lines enter the Remote parser.

## Session

There is one session. `HELLO` with `"v":1` moves the link from Disconnected to ConnectedUsb and answers `HELLO_ACK` with `ok` 1, `link` `usb`, and `support` 1. A second `HELLO` while connected answers `ERR` with reason `session` and stays connected. Any other version answers `HELLO_ACK` with `ok` 0 and `link` `none`, and stays disconnected. `GOODBYE` returns to Disconnected. If USB DTR drops while connected, the session returns to Disconnected. DTR by itself does not open a session.

Any operation other than `HELLO` while disconnected answers `ERR` with reason `closed`.

## Messages

`GET_STATE` answers `STATE` from `AppState`: screen, Wi-Fi phase, SSID, saved, scan, processed, candidates, observed, current, last, newest, elapsed, hosts, page, the canStart, canPause, and canResume flags, `profile`, and `ack`. `profile` is `basic`, `common`, or `detailed`. `ack` is the visible control name while a Remote press is showing, or an empty string. The snapshot has no passphrase, PSK, or vault field. `profile` and `ack` are optional for an older client that ignores unknown fields. The protocol version stays v1.

`ACTION` accepts only these names: `find`, `forget`, `start`, `pause`, `resume`, `stop`, `reset`, `hosts`, `settings`, `basic`, `common`, `detailed`, `back`, `next`, `prev`, `shift`, `page`, `del`, `ok`, `close`, and `row`. `row` requires `index` 0 through 5. An unknown name or an out-of-range index is rejected and does not call `applyAppAction`. The names added for Settings are ignored by an older client and do not change the existing result strings. An accepted action uses the same `applyAppAction` path as the touchscreen. When that action matches a control on the current screen, the panel paints the same pressed face used for touch and waits the existing 120 ms acknowledgement before the action runs. The wait does not block `loop()`. A second `ACTION` during that press answers `ACTION_RESULT` with `ok` 0 and `reason` `busy` and is not queued. An action with no visible control runs immediately and does not draw a button that is not on the screen. Pause and Resume are visible only in the matching scan state, so a Remote pause on an idle home screen does not invent a Pause button. `ok` 1 means the action was accepted. `Back` on the Networks screen returns to Home. `Back` on Settings returns to Home without closing a network list and without canceling a password. `Back` on the password screen still returns to the network list. Local touch stays available while a remote session is connected.

`GET_RESULTS` answers from the in-memory inventory, not from the SD card. The first `RESULT_ROW` is returned immediately when the inventory is not empty. Later rows are returned one per pass through `loop()`, followed by `RESULT_END`. The row fields match schema-1 CSV. An empty inventory answers `RESULT_END` with `count` 0. Another command during that stream answers `ERR` with reason `busy`.

`PING` answers `PONG` and echoes `id`.

Malformed JSON, an unknown operation, and an oversize line answer `ERR`. A connected session stays connected. The next well-formed frame is accepted.

## Limits

The parser does not run a shell, open a path, touch GPIO, update firmware, or offer a generic RPC. Physical USB is the v1 boundary. There is no pairing step, certificate, TLS session, or mDNS advertisement. Wi-Fi Remote transport is not implemented.
