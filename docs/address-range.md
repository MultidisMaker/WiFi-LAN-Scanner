# Address range

One scan batch contains at most 256 usable IPv4 addresses. The scanner remains the authority for that batch. Service Scan is unchanged and still opens no sockets.

## Modes

Automatic is the default. On a subnet whose usable hosts fit in one batch, it keeps the low window: hosts from the start of the subnet, excluding the station, the network address, and the broadcast address. On a larger subnet it does not start at the bottom. It selects the block aligned to the address count (64, 128, or 256) that contains the station, clips that block to the real mask, and skips the station. A /16 host ending in `.0` or `.255` stays eligible when it is not the subnet network or broadcast address. If the gateway is outside that block and the batch still has room, the gateway is added and the list is sorted. It does not replace a local address. The displayed range stays the local block, so a gateway outside that block does not move the displayed end. Next and previous step to the adjacent aligned block and never leave the joined subnet. Previous from the block after the station block returns to the station block.

Custom Start is an IPv4 address inside the joined subnet. The network address and the broadcast address are rejected. The station address is skipped and the batch continues at the next eligible host. A start outside the subnet is rejected and leaves the current batch unchanged. If fewer than the requested count remain before the broadcast address, the batch ends on the last eligible host and is marked clamped. It does not borrow addresses below the start.

Address Count is 64, 128, or 256. The default is 256. The firmware computes the end address. A batch never exceeds 256 and never crosses the usable subnet.

Next and previous move one batch inside the same subnet. Previous from the first custom window returns to Automatic. On a larger subnet, Automatic next and previous stay on aligned blocks. When the whole subnet fits in one batch, next and previous are hidden.

## What is stored

The batch size is stored in Preferences namespace `wls`, key `rcount`, as `64`, `128`, or `256`. A missing value stays 256 and is not written. Any other stored text is replaced with 256.

The mode and the custom start are session state. They are cleared when the joined network identity changes, including a disconnect, because a start address from another subnet is not safe to reuse. The stored count is kept.

## Remote

Remote protocol v1 uses the same scanner state. `STATE` adds `range`, `rangeStart`, `rangeEnd`, and `rangeLimit` before `ack`. Actions `range`, `automatic`, `custom`, `count64`, `count128`, `count256`, `windownext`, and `windowprev` call the same path as the touchscreen. `custom` carries `ip`. An invalid address returns `ok` 0 and does not arm the press acknowledgement. There is no password, PSK, or passphrase field.
