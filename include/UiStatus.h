#pragma once

#include <stddef.h>

#include "InventoryStore.h"
#include "ScannerController.h"

// Home and scan copy. Callers clip to the panel. These strings are the contract
// the host tests check. They do not include credentials.

bool formatScanBanner(char* out, size_t cap, ScanState state, bool stationReady, uint16_t processed,
                      uint16_t candidates, uint16_t observed);

// Short label for the existing 22-character progress control.
bool formatScanCard(char* out, size_t cap, ScanState state, bool stationReady, uint16_t processed,
                    uint16_t candidates);

// Full persistence sentence, including the logical path when a file was stored.
bool formatPersistStatus(char* out, size_t cap, const InventoryStoreResult& result);

// One size-1 panel line. A stored file says SD Saved and does not claim a mount
// that has not been proven. The full path stays on formatPersistStatus.
bool formatPersistPanel(char* out, size_t cap, const InventoryStoreResult& result);

// Home progress and device lines. "Addresses 256/256" fits a 22-character label.
bool formatAddressProgressLabel(char* out, size_t cap, uint16_t processed, uint16_t candidates);
bool formatDevicesFoundLabel(char* out, size_t cap, uint16_t observed);

// Home progress copy while Service Scan is active or finished. "Services 5120/5120" fits 22 glyphs.
bool formatServiceProgressLabel(char* out, size_t cap, uint16_t done, uint16_t planned);
bool formatServiceProgressDetail(char* out, size_t cap, const char* profileToken, uint16_t openPorts);
