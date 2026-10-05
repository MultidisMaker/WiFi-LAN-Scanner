#pragma once

#include <stdint.h>

// Host builds have no card. They keep returning Unavailable / contract-unproven.
// The device translation unit mounts the shared T-Display-S3-Pro SPI bus.
// It never formats or erases removable media.

enum class InventoryStoreStatus : uint8_t { Unavailable = 0, Stored = 1, Absent = 2, Failed = 3 };

struct InventoryStoreResult {
  InventoryStoreStatus status = InventoryStoreStatus::Unavailable;
  const char* detail = "contract-unproven";
};

class ScannerController;

InventoryStoreResult storeInventoryOnSd();

#ifndef WLS_TEST_MODE
#define WLS_TEST_MODE 0
#endif

#if defined(ARDUINO)
void bindInventoryScanner(const ScannerController* scanner);
#endif

#if WLS_TEST_MODE
struct SdProbeResult {
  const char* result = "fail";
  const char* stage = "unknown";
  uint32_t bytes = 0;
  bool match = false;
  bool removed = false;
  bool displayOk = false;
};

SdProbeResult probeSdMedia();
#endif
