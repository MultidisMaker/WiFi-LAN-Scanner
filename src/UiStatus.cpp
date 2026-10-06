#include "UiStatus.h"

#include <stdio.h>
#include <string.h>

namespace {

const char* scanWord(ScanState state, bool stationReady) {
  switch (state) {
    case ScanState::Idle:
      return stationReady ? "Ready" : "Idle";
    case ScanState::Starting:
      return "Starting";
    case ScanState::Scanning:
      return "Scanning";
    case ScanState::Paused:
      return "Paused";
    case ScanState::Stopping:
      return "Stopping";
    case ScanState::Complete:
      return "Complete";
  }
  return "Idle";
}

bool finish(char* out, size_t cap, int written) {
  if (out == nullptr || cap == 0 || written < 0 || static_cast<size_t>(written) >= cap) {
    if (out != nullptr && cap > 0) {
      out[0] = '\0';
    }
    return false;
  }
  return true;
}

}  // namespace

bool formatScanBanner(char* out, size_t cap, ScanState state, bool stationReady, uint16_t processed,
                      uint16_t candidates, uint16_t observed) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  if (state == ScanState::Idle && !stationReady) {
    return finish(out, cap, snprintf(out, cap, "Idle | join Wi-Fi before scanning"));
  }
  return finish(out, cap,
                snprintf(out, cap, "%s | %u/%u observed %u", scanWord(state, stationReady), processed, candidates,
                         observed));
}

bool formatScanCard(char* out, size_t cap, ScanState state, bool stationReady, uint16_t processed,
                    uint16_t candidates) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  if (state == ScanState::Idle && !stationReady) {
    return finish(out, cap, snprintf(out, cap, "Idle join Wi-Fi"));
  }
  return finish(out, cap, snprintf(out, cap, "%s %u/%u", scanWord(state, stationReady), processed, candidates));
}

bool formatPersistStatus(char* out, size_t cap, const InventoryStoreResult& result) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  if (result.status == InventoryStoreStatus::Stored && result.path[0] != '\0') {
    return finish(out, cap, snprintf(out, cap, "SD stored %s", result.path));
  }
  if (result.status == InventoryStoreStatus::Absent) {
    return finish(out, cap, snprintf(out, cap, "No SD"));
  }
  if (result.status == InventoryStoreStatus::Failed) {
    return finish(out, cap, snprintf(out, cap, "Save failed"));
  }
  return finish(out, cap, snprintf(out, cap, "No save yet"));
}

bool formatPersistPanel(char* out, size_t cap, const InventoryStoreResult& result) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  if (result.status == InventoryStoreStatus::Stored && result.path[0] != '\0') {
    return finish(out, cap, snprintf(out, cap, "SD Saved"));
  }
  return formatPersistStatus(out, cap, result);
}

bool formatAddressProgressLabel(char* out, size_t cap, uint16_t processed, uint16_t candidates) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  return finish(out, cap, snprintf(out, cap, "Addresses %u/%u", processed, candidates));
}

bool formatDevicesFoundLabel(char* out, size_t cap, uint16_t observed) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  return finish(out, cap, snprintf(out, cap, "Devices found %u", observed));
}

bool formatServiceProgressLabel(char* out, size_t cap, uint16_t done, uint16_t planned) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  return finish(out, cap, snprintf(out, cap, "Services %u/%u", done, planned));
}

bool formatServiceProgressDetail(char* out, size_t cap, const char* profileToken, uint16_t openHosts) {
  if (out == nullptr || cap < 8) {
    return false;
  }
  (void)profileToken;
  return finish(out, cap, snprintf(out, cap, "Open hosts %u", static_cast<unsigned>(openHosts)));
}
