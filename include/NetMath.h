#pragma once

#include <stdint.h>

struct Ipv4 {
  uint8_t octet[4] = {};
};

struct NetFacts {
  bool valid = false;
  bool hasSecondaryDns = false;
  Ipv4 address;
  Ipv4 mask;
  Ipv4 gateway;
  Ipv4 dnsPrimary;
  Ipv4 dnsSecondary;
  Ipv4 network;
  Ipv4 broadcast;
  uint8_t prefix = 0;
  uint32_t usableHosts = 0;
  uint32_t futureScanCap = 0;
  uint32_t futureScanCount = 0;
};

Ipv4 ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
bool ipv4Equal(const Ipv4& left, const Ipv4& right);

// Shared range derivation. Prefix must be 1..30 and the mask must be contiguous.
// Future scan count is min(usable hosts, kFutureScanHostCap). This does not probe.
NetFacts deriveNetFacts(const Ipv4& address, const Ipv4& mask, const Ipv4& gateway, const Ipv4& dnsPrimary,
                        const Ipv4& dnsSecondary);
