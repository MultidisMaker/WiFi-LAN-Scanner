#pragma once

#include "ServiceProfile.h"

// Device NVS for the Service Scan profile. Namespace "wls", key "profile".
// This is not the Wi-Fi passphrase namespace.
ServiceProfile loadServiceProfile(bool* fromNvs);
void saveServiceProfile(ServiceProfile profile);
