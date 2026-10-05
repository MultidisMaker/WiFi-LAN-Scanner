#pragma once

#include <stdint.h>

#include "Oui.h"

class ScannerController;

// Classifies MACs already stored in the inventory and attaches a manufacturer
// only from the local table. No network request is made. At most `budget`
// previously untouched hosts are updated per call.
void enrichObservedHosts(ScannerController& scanner, const OuiTable& table, uint16_t budget);
bool ouiEnrichmentIdle(const ScannerController& scanner);

// Device loop entry. Uses the embedded table and a budget of 32 hosts.
void serviceOuiEnrichment(ScannerController& scanner);
