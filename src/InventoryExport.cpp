#include "InventoryExport.h"

#include <stdio.h>
#include <string.h>

#include "NetMath.h"

namespace {

bool appendText(char* out, int cap, int* used, const char* text) {
  if (text == nullptr) {
    text = "";
  }
  for (int i = 0; text[i] != '\0'; ++i) {
    if (*used + 1 >= cap) {
      return false;
    }
    out[*used] = text[i];
    *used += 1;
  }
  out[*used] = '\0';
  return true;
}

bool appendChar(char* out, int cap, int* used, char value) {
  char text[2] = {value, '\0'};
  return appendText(out, cap, used, text);
}

void copyBounded(char* dest, size_t cap, const char* text) {
  size_t n = 0;
  if (dest == nullptr || cap == 0) {
    return;
  }
  if (text != nullptr) {
    for (; text[n] != '\0' && n + 1 < cap; ++n) {
      dest[n] = text[n];
    }
  }
  dest[n] = '\0';
}

bool needsQuote(const char* text) {
  if (text == nullptr) {
    return false;
  }
  for (int i = 0; text[i] != '\0'; ++i) {
    const char c = text[i];
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      return true;
    }
  }
  return false;
}

bool appendField(char* out, int cap, int* used, const char* text, bool last) {
  if (text == nullptr) {
    text = "";
  }
  if (needsQuote(text)) {
    if (!appendChar(out, cap, used, '"')) {
      return false;
    }
    for (int i = 0; text[i] != '\0'; ++i) {
      if (text[i] == '"') {
        if (!appendText(out, cap, used, "\"\"")) {
          return false;
        }
      } else if (!appendChar(out, cap, used, text[i])) {
        return false;
      }
    }
    if (!appendChar(out, cap, used, '"')) {
      return false;
    }
  } else if (!appendText(out, cap, used, text)) {
    return false;
  }
  if (!last && !appendChar(out, cap, used, ',')) {
    return false;
  }
  return true;
}

bool appendMeta(char* out, int cap, int* used, const char* key, const char* value) {
  if (!appendText(out, cap, used, "# ") || !appendText(out, cap, used, key) || !appendChar(out, cap, used, '=')) {
    return false;
  }
  if (value != nullptr) {
    for (int i = 0; value[i] != '\0'; ++i) {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      if (c < 32 || c == '#') {
        continue;
      }
      if (!appendChar(out, cap, used, static_cast<char>(c))) {
        return false;
      }
    }
  }
  return appendChar(out, cap, used, '\n');
}

}  // namespace

const char* inventoryCsvHeader() {
  return "ip,mac,method,name,nameSource,macClass,ouiState,manufacturer";
}

bool inventoryScanPath(char* out, size_t cap, uint32_t sequence) {
  if (out == nullptr || cap == 0) {
    return false;
  }
  const int n = snprintf(out, cap, "/WiFi-LAN-Scanner/scans/scan-%08lu.csv", static_cast<unsigned long>(sequence));
  return n > 0 && static_cast<size_t>(n) < cap;
}

void inventoryRowFromHost(InventoryRow& row, const ObservedHost& host) {
  row = InventoryRow();
  formatIpv4(host.ip, row.ip, sizeof(row.ip));
  if (host.hasMac) {
    formatMac(host.mac, row.mac, sizeof(row.mac));
  }
  copyBounded(row.method, sizeof(row.method), host.method);
  copyBounded(row.name, sizeof(row.name), host.name);
  copyBounded(row.nameSource, sizeof(row.nameSource), nameSourceLabel(host.nameSource));
  copyBounded(row.macClass, sizeof(row.macClass), macClassLabel(host.macClass));
  copyBounded(row.ouiState, sizeof(row.ouiState), ouiStateLabel(host.ouiState));
  if (host.ouiState == OuiState::Known && host.manufacturer != nullptr) {
    copyBounded(row.manufacturer, sizeof(row.manufacturer), host.manufacturer);
  }
}

bool formatInventoryPreamble(char* out, int cap, const InventoryMeta& meta) {
  if (out == nullptr || cap < 2) {
    return false;
  }
  int used = 0;
  out[0] = '\0';
  char schema[16];
  char sequence[16];
  char prefix[8];
  char candidates[8];
  char hostCap[8];
  snprintf(schema, sizeof(schema), "1");
  snprintf(sequence, sizeof(sequence), "%lu", static_cast<unsigned long>(meta.sequence));
  snprintf(prefix, sizeof(prefix), "%u", meta.prefix);
  snprintf(candidates, sizeof(candidates), "%u", meta.candidates);
  snprintf(hostCap, sizeof(hostCap), "%u", meta.cap);
  if (!appendMeta(out, cap, &used, "schema", schema) || !appendMeta(out, cap, &used, "sequence", sequence) ||
      !appendMeta(out, cap, &used, "station", meta.station) || !appendMeta(out, cap, &used, "prefix", prefix) ||
      !appendMeta(out, cap, &used, "gateway", meta.gateway) ||
      !appendMeta(out, cap, &used, "candidates", candidates) || !appendMeta(out, cap, &used, "cap", hostCap) ||
      !appendText(out, cap, &used, inventoryCsvHeader()) || !appendChar(out, cap, &used, '\n')) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool formatInventoryRowLine(char* out, int cap, const InventoryRow& row) {
  if (out == nullptr || cap < 2) {
    return false;
  }
  int used = 0;
  out[0] = '\0';
  if (!appendField(out, cap, &used, row.ip, false) || !appendField(out, cap, &used, row.mac, false) ||
      !appendField(out, cap, &used, row.method, false) || !appendField(out, cap, &used, row.name, false) ||
      !appendField(out, cap, &used, row.nameSource, false) || !appendField(out, cap, &used, row.macClass, false) ||
      !appendField(out, cap, &used, row.ouiState, false) || !appendField(out, cap, &used, row.manufacturer, true) ||
      !appendChar(out, cap, &used, '\n')) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool formatInventoryCsv(char* out, int cap, const InventoryMeta& meta, const InventoryRow* rows, int rowCount) {
  if (out == nullptr || cap < 2 || rowCount < 0 || (rowCount > 0 && rows == nullptr)) {
    return false;
  }
  if (!formatInventoryPreamble(out, cap, meta)) {
    out[0] = '\0';
    return false;
  }
  int used = static_cast<int>(strlen(out));
  for (int i = 0; i < rowCount; ++i) {
    char line[384];
    if (!formatInventoryRowLine(line, static_cast<int>(sizeof(line)), rows[i]) || !appendText(out, cap, &used, line)) {
      out[0] = '\0';
      return false;
    }
  }
  return true;
}

bool publishText(const PublishSink& sink, const char* finalPath, const char* text) {
  if (sink.write == nullptr || sink.rename == nullptr || finalPath == nullptr || text == nullptr) {
    return false;
  }
  char temporary[128];
  const int n = snprintf(temporary, sizeof(temporary), "%s.tmp", finalPath);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(temporary)) {
    return false;
  }
  if (!sink.write(sink.context, temporary, text)) {
    return false;
  }
  return sink.rename(sink.context, temporary, finalPath);
}
