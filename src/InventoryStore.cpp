#include "InventoryStore.h"

#if !defined(ARDUINO)
InventoryStoreResult storeInventoryOnSd() {
  InventoryStoreResult result;
  result.status = InventoryStoreStatus::Unavailable;
  result.detail = "contract-unproven";
  return result;
}
#endif
