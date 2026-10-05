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

// One size-1 panel line. Stored results keep the canonical directory visible.
bool formatPersistPanel(char* out, size_t cap, const InventoryStoreResult& result);
