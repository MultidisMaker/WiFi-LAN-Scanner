#include "ServiceProfile.h"

#include <string.h>

ServiceProfile defaultServiceProfile() { return ServiceProfile::Common; }

ServiceProfileValue parseServiceProfile(const char* text) {
  ServiceProfileValue parsed;
  if (text == nullptr) {
    return parsed;
  }
  if (strcmp(text, "basic") == 0) {
    parsed.profile = ServiceProfile::Basic;
    parsed.valid = true;
    return parsed;
  }
  if (strcmp(text, "common") == 0) {
    parsed.profile = ServiceProfile::Common;
    parsed.valid = true;
    return parsed;
  }
  if (strcmp(text, "detailed") == 0) {
    parsed.profile = ServiceProfile::Detailed;
    parsed.valid = true;
    return parsed;
  }
  return parsed;
}

const char* serviceProfileToken(ServiceProfile profile) {
  switch (profile) {
    case ServiceProfile::Basic:
      return "basic";
    case ServiceProfile::Detailed:
      return "detailed";
    case ServiceProfile::Common:
      return "common";
  }
  return "common";
}

const char* serviceProfileLabel(ServiceProfile profile) {
  switch (profile) {
    case ServiceProfile::Basic:
      return "Basic / fast";
    case ServiceProfile::Detailed:
      return "Detailed / slower";
    case ServiceProfile::Common:
      return "Common / recommended";
  }
  return "Common / recommended";
}

namespace {

struct ServicePortDef {
  uint16_t port;
  const char* family;
};

// Basic, then the Common additions, then the Detailed additions.
const ServicePortDef kPorts[] = {
    {22, "ssh"},    {80, "http"},     {443, "https"}, {445, "smb"},       {548, "afp"},
    {631, "ipp"},   {8080, "http-alt"}, {8443, "https-alt"}, {9100, "print"}, {21, "ftp"},
    {23, "telnet"}, {25, "smtp"},     {53, "dns"},    {110, "pop3"},      {143, "imap"},
    {587, "submission"}, {993, "imaps"}, {995, "pop3s"}, {1883, "mqtt"}, {8883, "mqtts"},
};

uint8_t portCount(ServiceProfile profile) {
  switch (profile) {
    case ServiceProfile::Basic:
      return 3;
    case ServiceProfile::Detailed:
      return 20;
    case ServiceProfile::Common:
      return 9;
  }
  return 9;
}

}  // namespace

uint8_t serviceProfilePortCount(ServiceProfile profile) { return portCount(profile); }

uint16_t serviceProfilePort(ServiceProfile profile, uint8_t index) {
  if (index >= portCount(profile) || index >= kServicePortCap) {
    return 0;
  }
  return kPorts[index].port;
}

const char* serviceProfilePortFamily(ServiceProfile profile, uint8_t index) {
  if (index >= portCount(profile) || index >= kServicePortCap) {
    return nullptr;
  }
  return kPorts[index].family;
}

uint32_t serviceProfileTimeoutMs(ServiceProfile profile) {
  switch (profile) {
    case ServiceProfile::Basic:
      return 200;
    case ServiceProfile::Detailed:
      return 300;
    case ServiceProfile::Common:
      return 250;
  }
  return 250;
}
