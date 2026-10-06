#include "AddressRangeStore.h"

#include <Preferences.h>
#include <string.h>

#include "AddressRange.h"

namespace {

constexpr const char* kNamespace = "wls";
constexpr const char* kKey = "rcount";

const char* tokenFor(uint16_t limit) {
  if (limit == 64) {
    return "64";
  }
  if (limit == 128) {
    return "128";
  }
  return "256";
}

}  // namespace

uint16_t loadAddressRangeCount(bool* fromNvs) {
  if (fromNvs != nullptr) {
    *fromNvs = false;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return 256;
  }
  char stored[8];
  stored[0] = '\0';
  prefs.getString(kKey, stored, sizeof(stored));
  prefs.end();
  if (stored[0] == '\0') {
    return 256;
  }
  if (strcmp(stored, "64") == 0 || strcmp(stored, "128") == 0 || strcmp(stored, "256") == 0) {
    if (fromNvs != nullptr) {
      *fromNvs = true;
    }
    return static_cast<uint16_t>(stored[0] == '6' ? 64 : stored[0] == '1' ? 128 : 256);
  }
  saveAddressRangeCount(256);
  return 256;
}

void saveAddressRangeCount(uint16_t limit) {
  if (!addressLimitOk(limit)) {
    return;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  prefs.putString(kKey, tokenFor(limit));
  prefs.end();
}
