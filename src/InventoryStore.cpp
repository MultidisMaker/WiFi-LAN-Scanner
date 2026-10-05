#include "InventoryStore.h"

#include <string.h>

namespace {
InventoryStoreResult gLastStore;
}

void rememberInventoryStore(const InventoryStoreResult& result) { gLastStore = result; }

const InventoryStoreResult& lastInventoryStore() { return gLastStore; }

#if !defined(ARDUINO)
InventoryStoreResult storeInventoryOnSd() {
  InventoryStoreResult result;
  result.status = InventoryStoreStatus::Unavailable;
  result.detail = "contract-unproven";
  result.path[0] = '\0';
  rememberInventoryStore(result);
  return result;
}
#endif
