#pragma once

#include "ServiceScan.h"

// One nonblocking TCP connect. Pause and stop are observed on the next poll.
// connect, select, and close only. This object never sends or receives payload.
class WifiTcpConnect : public ServiceConnectBackend {
 public:
  void start(const Ipv4& ip, uint16_t port, uint32_t nowMs, uint32_t timeoutMs) override;
  ServiceConnectStatus poll(uint32_t nowMs) override;
  void cancel() override;

 private:
  int fd_ = -1;
  uint32_t startedMs_ = 0;
  uint32_t timeoutMs_ = 0;
  bool waiting_ = false;
  ServiceConnectStatus held_ = ServiceConnectStatus::Pending;

  void closeSocket();
};
