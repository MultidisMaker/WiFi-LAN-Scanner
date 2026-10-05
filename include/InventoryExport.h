#pragma once

#include <stddef.h>
#include <stdint.h>

#include "HostInventory.h"

// Schema-1 CSV for positive observations only. Unanswered addresses are not rows.
// The device does not mount SD until the board pin contract is known.

struct InventoryMeta {
  uint32_t sequence = 0;
  char station[16] = {};
  uint8_t prefix = 0;
  char gateway[16] = {};
  uint16_t candidates = 0;
  uint16_t cap = 0;
};

struct InventoryRow {
  char ip[16] = {};
  char mac[18] = {};
  char method[16] = {};
  char name[32] = {};
  char nameSource[16] = {};
  char macClass[16] = {};
  char ouiState[16] = {};
  char manufacturer[65] = {};
};

struct PublishSink {
  bool (*write)(void* context, const char* path, const char* text) = nullptr;
  bool (*rename)(void* context, const char* fromPath, const char* toPath) = nullptr;
  void* context = nullptr;
};

const char* inventoryCsvHeader();
bool inventoryScanPath(char* out, size_t cap, uint32_t sequence);

// Manufacturer text is copied only when the host state is Known.
void inventoryRowFromHost(InventoryRow& row, const ObservedHost& host);

// Returns false when the text does not fit. Column order is fixed.
bool formatInventoryCsv(char* out, int cap, const InventoryMeta& meta, const InventoryRow* rows, int rowCount);

// Writes finalPath.tmp, then renames it to finalPath.
bool publishText(const PublishSink& sink, const char* finalPath, const char* text);
