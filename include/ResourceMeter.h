#pragma once

// Device-only. Prints one WLS resource line from the Arduino-ESP32 heap APIs.
void reportResource(const char* phase);

// Prints WLS psram-alloc=ok only when the external heap is about 8 MB, free
// space is nonzero, and a 64-byte ps_malloc block can be written and released.
void reportPsramProbe();
