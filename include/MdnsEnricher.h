#pragma once

#include <stdint.h>

class ScannerController;

// mDNS is disabled. serviceNameEnrichment does not call MDNS.begin.
// Reverse names come from DnsPtrEnricher after the ARP batch completes.
void serviceNameEnrichment(ScannerController& scanner);
bool nameEnrichmentIdle(const ScannerController& scanner);
uint16_t nameEnrichmentQueryCount();
