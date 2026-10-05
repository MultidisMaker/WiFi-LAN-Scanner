#include "ScanClock.h"

#include <Arduino.h>

uint32_t scanNow() { return static_cast<uint32_t>(millis()); }
