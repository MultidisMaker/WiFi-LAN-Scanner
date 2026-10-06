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
