#pragma once

#include <stdint.h>

// Device NVS for the address-batch size only. Namespace "wls", key "rcount".
// A custom start address is subnet-specific and is not stored.
uint16_t loadAddressRangeCount(bool* fromNvs);
void saveAddressRangeCount(uint16_t limit);
