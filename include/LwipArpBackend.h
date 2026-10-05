#pragma once

#include "DiscoveryBackend.h"

// One lwIP ARP request at a time on the station netif, then a read of etharp_find_addr.
// The installed ESP32-S3 lwIP default ARP_TABLE_SIZE is 10, so a pipelined sweep would
// evict replies before they are read. Silence is Unanswered, not Offline.
class LwipArpBackend : public DiscoveryBackend {
 public:
  void reset() override;
  void startProbe(const Ipv4& target, uint32_t nowMs) override;
  ProbeView poll(uint32_t nowMs) override;
  void cancel() override;
  const char* methodName() const override;

 private:
  bool active_ = false;
  bool blocked_ = false;
  Ipv4 target_;
  uint32_t startedMs_ = 0;
};
