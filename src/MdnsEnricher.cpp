#include "MdnsEnricher.h"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <mdns.h>
#include <stdio.h>
#include <string.h>

#include "NameRecord.h"
#include "NetMath.h"
#include "ScannerController.h"

namespace {

constexpr uint16_t kAttemptCap = 256;
constexpr uint32_t kQueryBudgetMs = 400;
constexpr uint32_t kMdnsTimeoutMs = 250;

Ipv4 attempts_[kAttemptCap];
uint16_t attemptCount_ = 0;
uint16_t queryCount_ = 0;
mdns_search_once_t* search_ = nullptr;
Ipv4 pendingIp_ = {};
uint32_t pendingStart_ = 0;
bool mdnsReady_ = false;

bool alreadyAttempted(const Ipv4& ip) {
  for (uint16_t i = 0; i < attemptCount_; ++i) {
    if (ipv4Equal(attempts_[i], ip)) {
      return true;
    }
  }
  return false;
}

void noteAttempt(const Ipv4& ip) {
  if (alreadyAttempted(ip)) {
    return;
  }
  if (attemptCount_ < kAttemptCap) {
    attempts_[attemptCount_++] = ip;
  }
  if (queryCount_ < 65535) {
    ++queryCount_;
  }
}

void clearAttempts() {
  attemptCount_ = 0;
  queryCount_ = 0;
}

const ObservedHost* nextUnnamed(const ScannerController& scanner) {
  for (uint16_t i = 0; i < scanner.observedCount(); ++i) {
    const ObservedHost* host = scanner.hostAt(i);
    if (host == nullptr || host->nameSource != NameSource::None || host->name[0] != '\0') {
      continue;
    }
    if (alreadyAttempted(host->ip)) {
      continue;
    }
    return host;
  }
  return nullptr;
}

void rememberResult(ScannerController& scanner, mdns_result_t* results) {
  const char* picked = nullptr;
  for (mdns_result_t* item = results; item != nullptr; item = item->next) {
    if (item->hostname != nullptr && item->hostname[0] != '\0') {
      picked = item->hostname;
      break;
    }
  }
  if (picked == nullptr) {
    for (mdns_result_t* item = results; item != nullptr; item = item->next) {
      if (item->instance_name != nullptr && item->instance_name[0] != '\0' && item->instance_name[0] != '_') {
        picked = item->instance_name;
        break;
      }
    }
  }
  if (picked != nullptr) {
    scanner.rememberName(pendingIp_, picked, NameSource::Mdns);
  }
}

void releaseSearch(ScannerController* scanner, bool apply) {
  if (search_ == nullptr) {
    return;
  }
  mdns_result_t* results = nullptr;
  const bool done = mdns_query_async_get_results(search_, 0, &results);
  if (done) {
    if (apply && scanner != nullptr) {
      rememberResult(*scanner, results);
    }
    if (results != nullptr) {
      mdns_query_results_free(results);
    }
    mdns_query_async_delete(search_);
    search_ = nullptr;
    noteAttempt(pendingIp_);
    return;
  }
  if (mdns_query_async_delete(search_) == ESP_OK) {
    search_ = nullptr;
    noteAttempt(pendingIp_);
  }
}

void pollSearch(ScannerController& scanner) {
  if (search_ == nullptr) {
    return;
  }
  const bool expired = static_cast<uint32_t>(millis() - pendingStart_) >= kQueryBudgetMs;
  mdns_result_t* results = nullptr;
  const bool done = mdns_query_async_get_results(search_, 0, &results);
  if (!done && !expired) {
    return;
  }
  if (done) {
    rememberResult(scanner, results);
    if (results != nullptr) {
      mdns_query_results_free(results);
    }
    mdns_query_async_delete(search_);
    search_ = nullptr;
    noteAttempt(pendingIp_);
    return;
  }
  if (mdns_query_async_delete(search_) == ESP_OK) {
    search_ = nullptr;
    noteAttempt(pendingIp_);
  }
}

void startQuery(const Ipv4& ip) {
  char qname[40];
  snprintf(qname, sizeof(qname), "%u.%u.%u.%u.in-addr.arpa", static_cast<unsigned>(ip.octet[3]),
           static_cast<unsigned>(ip.octet[2]), static_cast<unsigned>(ip.octet[1]), static_cast<unsigned>(ip.octet[0]));
  pendingIp_ = ip;
  pendingStart_ = millis();
  // Named reverse PTR only. service_type and proto stay null so this is not a service browse.
  search_ = mdns_query_async_new(qname, nullptr, nullptr, MDNS_TYPE_PTR, kMdnsTimeoutMs, 1, nullptr);
  if (search_ == nullptr) {
    noteAttempt(ip);
  }
}

void stopMdnsWhileDiscovering() {
  releaseSearch(nullptr, false);
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
#if WLS_TEST_MODE
  // HIL discovery runs are the ARP measurement. The same IDF parser panic
  // (LoadProhibited in _mdns_search_find_from) rebooted a live repeat, so the
  // test image does not start mDNS. Production still resolves names after the
  // batch leaves the scanning states.
  (void)scanner;
  stopMdnsWhileDiscovering();
  return;
#endif
  if (WiFi.status() != WL_CONNECTED) {
    releaseSearch(nullptr, false);
    return;
  }
  const ScanState phase = scanner.state();
  if (phase == ScanState::Starting || phase == ScanState::Scanning || phase == ScanState::Paused ||
      phase == ScanState::Stopping) {
    stopMdnsWhileDiscovering();
    return;
  }
  if (scanner.observedCount() == 0) {
    releaseSearch(nullptr, false);
    clearAttempts();
    return;
  }
  if (search_ != nullptr) {
    pollSearch(scanner);
    return;
  }
  const ObservedHost* host = nextUnnamed(scanner);
  if (host == nullptr) {
    return;
  }
  if (!mdnsReady_) {
    mdnsReady_ = MDNS.begin("wls");
    if (!mdnsReady_) {
      return;
    }
  }
  startQuery(host->ip);
}

bool nameEnrichmentIdle(const ScannerController& scanner) {
#if WLS_TEST_MODE
  (void)scanner;
  return true;
#endif
  if (WiFi.status() != WL_CONNECTED) {
    return true;
  }
  if (search_ != nullptr) {
    return false;
  }
  return nextUnnamed(scanner) == nullptr;
}

uint16_t nameEnrichmentQueryCount() { return queryCount_; }
