#include "InventoryStore.h"

InventoryStoreResult storeInventoryOnSd() {
  InventoryStoreResult result;
  result.status = InventoryStoreStatus::Unavailable;
  result.detail = "contract-unproven";
  return result;
}
