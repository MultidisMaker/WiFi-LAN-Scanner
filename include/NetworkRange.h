#pragma once

#include <Arduino.h>

struct NetworkRange {
  bool valid = false;
  IPAddress address;
  IPAddress mask;
  IPAddress gateway;
  IPAddress dnsPrimary;
  IPAddress dnsSecondary;
  bool hasSecondaryDns = false;
  IPAddress network;
  IPAddress broadcast;
  uint8_t prefix = 0;
  uint32_t usableHosts = 0;
  uint32_t futureScanCap = 0;
  uint32_t futureScanCount = 0;
};

NetworkRange deriveRange(const IPAddress& address, const IPAddress& mask, const IPAddress& gateway,
                         const IPAddress& dnsPrimary, const IPAddress& dnsSecondary);
NetworkRange rangeFromStation();
bool networkRangeSelfTest();
void formatIp(const IPAddress& ip, char* out, size_t outLen);
