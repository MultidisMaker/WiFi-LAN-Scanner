#pragma once

#include <stdint.h>

// The installed GFX LILYGO_T_DISPLAY_S3_PRO example and the touch profile do not
// name an SD chip-select or SDMMC bus. This entry point therefore does not call
// SD.begin, does not configure GPIO, and does not format or erase media.

enum class InventoryStoreStatus : uint8_t { Unavailable = 0, Stored = 1 };

struct InventoryStoreResult {
  InventoryStoreStatus status = InventoryStoreStatus::Unavailable;
  const char* detail = "contract-unproven";
};

InventoryStoreResult storeInventoryOnSd();
