#pragma once

#include "NetMath.h"

enum class EvidenceRank : uint8_t { None = 0, Answered = 1, Neighbor = 2 };

struct ObservedHost {
  Ipv4 ip;
  bool hasMac = false;
  uint8_t mac[6] = {};
  EvidenceRank evidence = EvidenceRank::None;
  bool hasLatency = false;
  uint32_t latencyMs = 0;
  uint32_t firstSeenMs = 0;
  uint32_t lastSeenMs = 0;
  const char* method = "";
};

// In-memory only. Reboot clears it. Unanswered probes are not stored.
class HostInventory {
 public:
  static constexpr uint16_t kCap = 256;

  void clear();
  uint16_t count() const;
  const ObservedHost* at(uint16_t index) const;
  const ObservedHost* newest() const;
  void observe(const Ipv4& ip, EvidenceRank evidence, bool hasMac, const uint8_t mac[6], bool hasLatency,
               uint32_t latencyMs, uint32_t seenMs, const char* method);

 private:
  ObservedHost hosts_[kCap] = {};
  uint16_t count_ = 0;
  int newest_ = -1;

  void merge(ObservedHost& dest, const Ipv4& ip, EvidenceRank evidence, bool hasMac, const uint8_t mac[6],
             bool hasLatency, uint32_t latencyMs, uint32_t seenMs, const char* method, bool replaceIdentity);
};
