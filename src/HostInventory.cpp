#include "HostInventory.h"

#include <string.h>

void HostInventory::clear() {
  count_ = 0;
  newest_ = -1;
}

uint16_t HostInventory::count() const { return count_; }

const ObservedHost* HostInventory::at(uint16_t index) const {
  if (index >= count_) {
    return nullptr;
  }
  return &hosts_[index];
}

const ObservedHost* HostInventory::newest() const {
  if (newest_ < 0 || static_cast<uint16_t>(newest_) >= count_) {
    return nullptr;
  }
  return &hosts_[newest_];
}

void HostInventory::merge(ObservedHost& dest, const Ipv4& ip, EvidenceRank evidence, bool hasMac, const uint8_t mac[6],
                          bool hasLatency, uint32_t latencyMs, uint32_t seenMs, const char* method,
                          bool replaceIdentity) {
  if (replaceIdentity) {
    dest.ip = ip;
  }
  dest.lastSeenMs = seenMs;
  if (evidence >= dest.evidence && hasMac && mac != nullptr) {
    const bool sameMac = dest.hasMac && memcmp(dest.mac, mac, 6) == 0;
    dest.hasMac = true;
    memcpy(dest.mac, mac, 6);
    dest.evidence = evidence;
    if (method != nullptr) {
      dest.method = method;
    }
    if (!sameMac) {
      dest.macClass = classifyMac(mac);
      dest.ouiState = OuiState::Unset;
      dest.manufacturer = nullptr;
    }
  } else if (evidence > dest.evidence) {
    dest.evidence = evidence;
    if (method != nullptr) {
      dest.method = method;
    }
  }
  if (hasLatency && evidence >= dest.evidence) {
    dest.hasLatency = true;
    dest.latencyMs = latencyMs;
  }
}

void HostInventory::observe(const Ipv4& ip, EvidenceRank evidence, bool hasMac, const uint8_t mac[6], bool hasLatency,
                            uint32_t latencyMs, uint32_t seenMs, const char* method) {
  if (evidence == EvidenceRank::None) {
    return;
  }
  for (uint16_t i = 0; i < count_; ++i) {
    if (ipv4Equal(hosts_[i].ip, ip)) {
      merge(hosts_[i], ip, evidence, hasMac, mac, hasLatency, latencyMs, seenMs, method, false);
      newest_ = static_cast<int>(i);
      return;
    }
  }
  if (hasMac && mac != nullptr) {
    for (uint16_t i = 0; i < count_; ++i) {
      if (hosts_[i].hasMac && memcmp(hosts_[i].mac, mac, 6) == 0) {
        merge(hosts_[i], ip, evidence, hasMac, mac, hasLatency, latencyMs, seenMs, method, true);
        newest_ = static_cast<int>(i);
        return;
      }
    }
  }
  if (count_ >= kCap) {
    return;
  }
  ObservedHost& dest = hosts_[count_];
  dest = ObservedHost();
  dest.ip = ip;
  dest.firstSeenMs = seenMs;
  dest.lastSeenMs = seenMs;
  dest.evidence = evidence;
  dest.method = method == nullptr ? "" : method;
  dest.hasMac = hasMac && mac != nullptr;
  if (dest.hasMac) {
    memcpy(dest.mac, mac, 6);
    dest.macClass = classifyMac(mac);
    dest.ouiState = OuiState::Unset;
    dest.manufacturer = nullptr;
  } else {
    dest.macClass = MacClass::Absent;
    dest.ouiState = OuiState::None;
    dest.manufacturer = nullptr;
  }
  dest.hasLatency = hasLatency;
  dest.latencyMs = latencyMs;
  newest_ = static_cast<int>(count_);
  ++count_;
}

NameApply HostInventory::rememberName(const Ipv4& ip, const char* raw, NameSource source) {
  ObservedHost* dest = nullptr;
  for (uint16_t i = 0; i < count_; ++i) {
    if (ipv4Equal(hosts_[i].ip, ip)) {
      dest = &hosts_[i];
      break;
    }
  }
  if (dest == nullptr) {
    return NameApply::MissingHost;
  }
  char cleaned[32];
  if (!sanitizeHostName(raw, cleaned, sizeof(cleaned))) {
    return NameApply::Rejected;
  }
  if (!preferIncomingName(dest->nameSource, dest->name, source, cleaned)) {
    return NameApply::Kept;
  }
  memset(dest->name, 0, sizeof(dest->name));
  memcpy(dest->name, cleaned, strlen(cleaned) + 1);
  dest->nameSource = source;
  return NameApply::Applied;
}

void HostInventory::enrichManufacturer(uint16_t index, const OuiTable& table) {
  if (index >= count_) {
    return;
  }
  ObservedHost& host = hosts_[index];
  if (!host.hasMac) {
    host.macClass = MacClass::Absent;
    host.ouiState = OuiState::None;
    host.manufacturer = nullptr;
    return;
  }
  const OuiResult result = lookupOui(table, host.mac);
  host.macClass = result.macClass;
  host.ouiState = result.state;
  host.manufacturer = result.name;
}
