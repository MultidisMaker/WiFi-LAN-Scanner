#pragma once

#include <stdint.h>

// Scanner-owned Service Scan profile. Tokens and NVS behavior are unchanged.
// Port lists below are the connect-only v1 contract.
enum class ServiceProfile : uint8_t { Basic = 0, Common = 1, Detailed = 2 };

static constexpr uint8_t kServicePortCap = 20;

struct ServiceProfileValue {
  ServiceProfile profile = ServiceProfile::Common;
  bool valid = false;
};

ServiceProfile defaultServiceProfile();
ServiceProfileValue parseServiceProfile(const char* text);
const char* serviceProfileToken(ServiceProfile profile);
const char* serviceProfileLabel(ServiceProfile profile);

// Cumulative ordered TCP ports. Basic is the first 3, Common the first 9,
// Detailed all 20. An out-of-range index returns 0 and a null family.
uint8_t serviceProfilePortCount(ServiceProfile profile);
uint16_t serviceProfilePort(ServiceProfile profile, uint8_t index);
const char* serviceProfilePortFamily(ServiceProfile profile, uint8_t index);
uint32_t serviceProfileTimeoutMs(ServiceProfile profile);
