#pragma once

#include "HostInventory.h"

enum class ProbeStatus : uint8_t { Idle, Pending, Observed, Unanswered };

struct ProbeView {
  ProbeStatus status = ProbeStatus::Idle;
  bool hasMac = false;
  uint8_t mac[6] = {};
  bool hasLatency = false;
  uint32_t latencyMs = 0;
  EvidenceRank evidence = EvidenceRank::None;
  const char* method = "";
};

// Transport behind the scanner. The UI never calls this.
class DiscoveryBackend {
 public:
  virtual ~DiscoveryBackend() = default;
  virtual void reset() = 0;
  virtual void startProbe(const Ipv4& target, uint32_t nowMs) = 0;
  virtual ProbeView poll(uint32_t nowMs) = 0;
  virtual void cancel() = 0;
  virtual const char* methodName() const = 0;
};
