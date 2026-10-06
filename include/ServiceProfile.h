#pragma once

#include <stdint.h>

// Scanner-owned Service Scan profile. This is a setting only.
// A013 does not open TCP or UDP sockets for service probes.
enum class ServiceProfile : uint8_t { Basic = 0, Common = 1, Detailed = 2 };

struct ServiceProfileValue {
  ServiceProfile profile = ServiceProfile::Common;
  bool valid = false;
};

ServiceProfile defaultServiceProfile();
ServiceProfileValue parseServiceProfile(const char* text);
const char* serviceProfileToken(ServiceProfile profile);
const char* serviceProfileLabel(ServiceProfile profile);
