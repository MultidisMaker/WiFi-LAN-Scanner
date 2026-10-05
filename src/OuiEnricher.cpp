#include "OuiEnricher.h"

#include "OuiData.h"
#include "ScannerController.h"

namespace {
uint16_t gCursor = 0;
}

void enrichObservedHosts(ScannerController& scanner, const OuiTable& table, uint16_t budget) {
  const uint16_t count = scanner.observedCount();
  if (count == 0) {
    gCursor = 0;
    return;
  }
  if (budget == 0) {
    budget = 1;
  }
  uint16_t steps = 0;
  uint16_t visited = 0;
  while (steps < budget && visited < count) {
    if (gCursor >= count) {
      gCursor = 0;
    }
    const ObservedHost* host = scanner.hostAt(gCursor);
    if (host != nullptr && host->ouiState == OuiState::Unset) {
      scanner.enrichManufacturer(gCursor, table);
      ++steps;
    }
    ++gCursor;
    ++visited;
  }
}

bool ouiEnrichmentIdle(const ScannerController& scanner) {
  const uint16_t count = scanner.observedCount();
  for (uint16_t i = 0; i < count; ++i) {
    const ObservedHost* host = scanner.hostAt(i);
    if (host != nullptr && host->ouiState == OuiState::Unset) {
      return false;
    }
  }
  return true;
}

void serviceOuiEnrichment(ScannerController& scanner) {
  enrichObservedHosts(scanner, embeddedOuiTable(), 32);
}
