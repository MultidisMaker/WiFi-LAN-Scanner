#include "NetMath.h"

#include "BoardConfig.h"

namespace {
uint32_t pack(const Ipv4& ip) {
  return (uint32_t(ip.octet[0]) << 24) | (uint32_t(ip.octet[1]) << 16) | (uint32_t(ip.octet[2]) << 8) |
         uint32_t(ip.octet[3]);
}

Ipv4 unpack(uint32_t value) {
  return ipv4(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
              static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
}

uint8_t prefixOf(uint32_t mask, bool& contiguous) {
  uint8_t bits = 0;
  bool zeroSeen = false;
  contiguous = true;
  for (int shift = 31; shift >= 0; --shift) {
    const bool one = ((mask >> shift) & 1U) != 0;
    if (one) {
      if (zeroSeen) {
        contiguous = false;
        return 0;
      }
      ++bits;
    } else {
      zeroSeen = true;
    }
  }
  return bits;
}
}

Ipv4 ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  Ipv4 ip;
  ip.octet[0] = a;
  ip.octet[1] = b;
  ip.octet[2] = c;
  ip.octet[3] = d;
  return ip;
}

bool ipv4Equal(const Ipv4& left, const Ipv4& right) {
  return left.octet[0] == right.octet[0] && left.octet[1] == right.octet[1] && left.octet[2] == right.octet[2] &&
         left.octet[3] == right.octet[3];
}

NetFacts deriveNetFacts(const Ipv4& address, const Ipv4& mask, const Ipv4& gateway, const Ipv4& dnsPrimary,
                        const Ipv4& dnsSecondary) {
  NetFacts facts;
  facts.address = address;
  facts.mask = mask;
  facts.gateway = gateway;
  facts.dnsPrimary = dnsPrimary;
  facts.dnsSecondary = dnsSecondary;
  facts.hasSecondaryDns = pack(dnsSecondary) != 0;
  facts.futureScanCap = kFutureScanHostCap;
  bool contiguous = false;
  const uint8_t prefix = prefixOf(pack(mask), contiguous);
  if (!contiguous || prefix == 0 || prefix > 30) {
    return facts;
  }
  const uint32_t maskValue = pack(mask);
  const uint32_t network = pack(address) & maskValue;
  const uint32_t broadcast = network | ~maskValue;
  const uint8_t hostBits = static_cast<uint8_t>(32 - prefix);
  const uint64_t usable = (1ULL << hostBits) - 2ULL;
  facts.valid = true;
  facts.prefix = prefix;
  facts.network = unpack(network);
  facts.broadcast = unpack(broadcast);
  facts.usableHosts = usable > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : static_cast<uint32_t>(usable);
  facts.futureScanCount = facts.usableHosts < kFutureScanHostCap ? facts.usableHosts : kFutureScanHostCap;
  return facts;
}
