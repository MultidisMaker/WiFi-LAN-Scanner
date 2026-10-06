#pragma once

#include <stdint.h>

#include "ScannerController.h"
#include "ServiceProfile.h"
#include "UiModel.h"

// Logical actions shared by the touchscreen and USB Remote protocol v1.
// Wi-Fi transport, TLS, and the proprietary Remote application are not attached.
enum class AppAction : uint8_t {
  None = 0,
  FindNetworks,
  ForgetNetwork,
  StartScan,
  PauseScan,
  ResumeScan,
  StopScan,
  ResetScan,
  OpenHosts,
  Back,
  NextPage,
  PrevPage,
  Shift,
  KeyboardPage,
  Backspace,
  SubmitPassword,
  CancelPassword,
  SelectRow,
  OpenSettings,
  SetProfile,
  OpenService,
  OpenRange,
  SetAutomatic,
  SetCustom,
  SetLimit,
  WindowNext,
  WindowPrev
};

enum class AppScreen : uint8_t { Home, Results, Entry, Hosts, Settings };

struct AppView {
  bool showingHosts = false;
  bool showingSettings = false;
  bool resultsOpen = false;
  bool entryOpen = false;
  int page = 0;
  int keyboardPage = 0;
  int rowOffset = 0;
  int resultCount = 0;
  uint16_t observedCount = 0;
  ServiceProfile profile = ServiceProfile::Common;
  SettingsPage settingsPage = SettingsPage::Menu;
};

struct AppHooks {
  void (*findNetworks)(void* context) = nullptr;
  void (*forgetNetwork)(void* context) = nullptr;
  void (*selectResult)(void* context, int index) = nullptr;
  void (*toggleShift)(void* context) = nullptr;
  void (*backspace)(void* context) = nullptr;
  void (*submitPassword)(void* context) = nullptr;
  void (*cancelPassword)(void* context) = nullptr;
  void (*closeResults)(void* context) = nullptr;
  void (*setProfile)(void* context, int profileIndex) = nullptr;
  void* context = nullptr;
};

struct AppWifiView {
  const char* phase = "idle";
  const char* ssid = "";
  bool saved = false;
  bool shift = false;
  bool entry = false;
  bool results = false;
};

struct AppState {
  AppScreen screen = AppScreen::Home;
  char wifiPhase[16] = {};
  char ssid[33] = {};
  bool saved = false;
  char scan[16] = {};
  uint16_t processed = 0;
  uint16_t candidates = 0;
  uint16_t observed = 0;
  char current[16] = {};
  char last[16] = {};
  char newest[16] = {};
  uint32_t elapsedMs = 0;
  bool hostsOpen = false;
  int page = 0;
  int keyboardPage = 0;
  bool shift = false;
  bool canStart = false;
  bool canPause = false;
  bool canResume = false;
  // Control name while a Remote press is showing. Empty when none is showing.
  char ack[12] = {};
  // Service Scan profile token. Not a credential. The diagnostic line omits it
  // so the existing 240-byte HIL buffer stays large enough.
  char profile[12] = {};
  // Address-range fields are not credentials. The diagnostic line omits them
  // so the existing 240-byte HIL buffer stays large enough.
  char rangeMode[12] = {};
  char rangeStart[16] = {};
  char rangeEnd[16] = {};
  uint16_t rangeLimit = 256;
};

// Row ids 200..205 become SelectRow. Other known controls map to one action.
// A null rowOffset is ignored. SelectRow writes the 0..5 offset there.
AppAction actionFromControl(int id, int* rowOffset);

// One behavior implementation. Null Wi-Fi hooks skip those calls.
bool applyAppAction(AppAction action, AppView& view, ScannerController& scanner, const AppHooks* hooks,
                    const char* text = nullptr);

void fillAppState(AppState& out, const AppView& view, const ScannerController& scanner, const AppWifiView& wifi,
                  ServiceProfile profile = ServiceProfile::Common);

// One diagnostic line. It has no passphrase field. Returns length, or -1.
int formatAppStateLine(char* out, int cap, const AppState& state);
