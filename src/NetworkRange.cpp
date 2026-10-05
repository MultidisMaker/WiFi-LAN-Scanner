#include "NetworkRange.h"

#include <WiFi.h>

#include "BoardConfig.h"

namespace {
uint32_t pack(const IPAddress& ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | uint32_t(ip[3]);
}

IPAddress unpack(uint32_t value) {
  return IPAddress((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
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

void formatIp(const IPAddress& ip, char* out, size_t outLen) {
  snprintf(out, outLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

NetworkRange deriveRange(const IPAddress& address, const IPAddress& mask, const IPAddress& gateway,
                         const IPAddress& dnsPrimary, const IPAddress& dnsSecondary) {
  NetworkRange range;
  range.address = address;
  range.mask = mask;
  range.gateway = gateway;
  range.dnsPrimary = dnsPrimary;
  range.dnsSecondary = dnsSecondary;
  range.hasSecondaryDns = pack(dnsSecondary) != 0;
  range.futureScanCap = kFutureScanHostCap;
  bool contiguous = false;
  const uint32_t maskValue = pack(mask);
  const uint8_t prefix = prefixOf(maskValue, contiguous);
  if (!contiguous || prefix == 0 || prefix > 30) {
    return range;
  }
  const uint32_t ipValue = pack(address);
  const uint32_t network = ipValue & maskValue;
  const uint32_t broadcast = network | ~maskValue;
  const uint8_t hostBits = static_cast<uint8_t>(32 - prefix);
  const uint64_t usable = (1ULL << hostBits) - 2ULL;
  range.valid = true;
  range.prefix = prefix;
  range.network = unpack(network);
  range.broadcast = unpack(broadcast);
  range.usableHosts = usable > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : static_cast<uint32_t>(usable);
  range.futureScanCount = range.usableHosts < kFutureScanHostCap ? range.usableHosts : kFutureScanHostCap;
  return range;
}

NetworkRange rangeFromStation() {
  if (WiFi.status() != WL_CONNECTED) {
    return NetworkRange();
  }
  return deriveRange(WiFi.localIP(), WiFi.subnetMask(), WiFi.gatewayIP(), WiFi.dnsIP(0), WiFi.dnsIP(1));
}

bool networkRangeSelfTest() {
  const NetworkRange slash24 = deriveRange(IPAddress(192, 168, 0, 20), IPAddress(255, 255, 255, 0),
                                            IPAddress(192, 168, 0, 1), IPAddress(192, 168, 0, 1), IPAddress(0, 0, 0, 0));
  const NetworkRange slash28 = deriveRange(IPAddress(10, 0, 0, 5), IPAddress(255, 255, 255, 240),
                                            IPAddress(10, 0, 0, 1), IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
  const NetworkRange slash16 = deriveRange(IPAddress(10, 1, 0, 9), IPAddress(255, 255, 0, 0), IPAddress(10, 1, 0, 1),
                                            IPAddress(10, 1, 0, 1), IPAddress(0, 0, 0, 0));
  const NetworkRange broken = deriveRange(IPAddress(10, 0, 0, 1), IPAddress(255, 0, 255, 0), IPAddress(10, 0, 0, 1),
                                           IPAddress(10, 0, 0, 1), IPAddress(0, 0, 0, 0));
  const bool ok = slash24.valid && slash24.prefix == 24 && slash24.network == IPAddress(192, 168, 0, 0) &&
                  slash24.usableHosts == 254 && slash24.futureScanCount == 254 && slash28.valid && slash28.prefix == 28 &&
                  slash28.usableHosts == 14 && slash28.futureScanCount == 14 && slash28.hasSecondaryDns && slash16.valid &&
                  slash16.prefix == 16 && slash16.futureScanCount == kFutureScanHostCap && !broken.valid;
  Serial.printf("WLS range-selftest=%s cap=%lu\n", ok ? "ok" : "fail", static_cast<unsigned long>(kFutureScanHostCap));
  return ok;
}
