#include "ServiceProfileStore.h"

#include <Preferences.h>
#include <string.h>

namespace {

constexpr const char* kNamespace = "wls";
constexpr const char* kKey = "profile";

}  // namespace

ServiceProfile loadServiceProfile(bool* fromNvs) {
  if (fromNvs != nullptr) {
    *fromNvs = false;
  }
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return defaultServiceProfile();
  }
  char stored[16];
  stored[0] = '\0';
  prefs.getString(kKey, stored, sizeof(stored));
  prefs.end();
  if (stored[0] == '\0') {
    return defaultServiceProfile();
  }
  const ServiceProfileValue parsed = parseServiceProfile(stored);
  if (!parsed.valid) {
    saveServiceProfile(defaultServiceProfile());
    return defaultServiceProfile();
  }
  if (fromNvs != nullptr) {
    *fromNvs = true;
  }
  return parsed.profile;
}

void saveServiceProfile(ServiceProfile profile) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return;
  }
  prefs.putString(kKey, serviceProfileToken(profile));
  prefs.end();
}
