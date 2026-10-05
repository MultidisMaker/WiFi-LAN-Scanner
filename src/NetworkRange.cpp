#include "NetworkRange.h"

#include <WiFi.h>

#include "BoardConfig.h"
#include "NetMath.h"

namespace {
Ipv4 toIpv4(const IPAddress& ip) { return ipv4(ip[0], ip[1], ip[2], ip[3]); }

IPAddress fromIpv4(const Ipv4& ip) { return IPAddress(ip.octet[0], ip.octet[1], ip.octet[2], ip.octet[3]); }
}

void formatIp(const IPAddress& ip, char* out, size_t outLen) {
  snprintf(out, outLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

NetworkRange deriveRange(const IPAddress& address, const IPAddress& mask, const IPAddress& gateway,
                         const IPAddress& dnsPrimary, const IPAddress& dnsSecondary) {
  const NetFacts facts = deriveNetFacts(toIpv4(address), toIpv4(mask), toIpv4(gateway), toIpv4(dnsPrimary),
                                        toIpv4(dnsSecondary));
  NetworkRange range;
  range.valid = facts.valid;
  range.address = fromIpv4(facts.address);
  range.mask = fromIpv4(facts.mask);
  range.gateway = fromIpv4(facts.gateway);
  range.dnsPrimary = fromIpv4(facts.dnsPrimary);
  range.dnsSecondary = fromIpv4(facts.dnsSecondary);
  range.hasSecondaryDns = facts.hasSecondaryDns;
  range.network = fromIpv4(facts.network);
  range.broadcast = fromIpv4(facts.broadcast);
  range.prefix = facts.prefix;
  range.usableHosts = facts.usableHosts;
  range.futureScanCap = facts.futureScanCap;
  range.futureScanCount = facts.futureScanCount;
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
