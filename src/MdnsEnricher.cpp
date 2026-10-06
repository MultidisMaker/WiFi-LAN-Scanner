#include "MdnsEnricher.h"

#include <ESPmDNS.h>

#include "ScannerController.h"

namespace {

bool mdnsReady_ = false;

void stopMdnsWhileDiscovering() {
  if (!mdnsReady_) {
    return;
  }
  // mdns_free stops the parser task. A hostile or partial mDNS packet otherwise
  // panics that task inside ESP-IDF and reboots the scanner mid-batch.
  MDNS.end();
  mdnsReady_ = false;
}

}  // namespace

void serviceNameEnrichment(ScannerController& scanner) {
  // mDNS stays off in this increment. The ESP-IDF parser panic in
  // _mdns_search_find_from is isolated by never calling MDNS.begin.
  (void)scanner;
  stopMdnsWhileDiscovering();
}

bool nameEnrichmentIdle(const ScannerController& scanner) {
  (void)scanner;
  return true;
}

uint16_t nameEnrichmentQueryCount() { return 0; }
