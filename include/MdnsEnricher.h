#pragma once

#include <stdint.h>

class ScannerController;

// One in-flight link-local mDNS reverse PTR for a host the scanner has already observed.
// This does not browse services and does not send reverse DNS.
void serviceNameEnrichment(ScannerController& scanner);
bool nameEnrichmentIdle(const ScannerController& scanner);
uint16_t nameEnrichmentQueryCount();
