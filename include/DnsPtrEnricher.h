#pragma once

#include <stdint.h>

class ScannerController;

// One in-flight DNS PTR query for a host the ARP scan already observed.
// Queries start only after the batch leaves Starting, Scanning, Paused, and Stopping.
void serviceDnsEnrichment(ScannerController& scanner);
bool dnsEnrichmentIdle(const ScannerController& scanner);
uint16_t dnsEnrichmentAttemptCount();
