#pragma once

#include "ScannerController.h"
#include "ServiceProfile.h"
#include "UiPress.h"

enum UiControlId : int {
  IdFind = 1,
  IdForget,
  IdStart,
  IdPause,
  IdResume,
  IdStop,
  IdPrev,
  IdNext,
  IdBack,
  IdShift,
  IdPage,
  IdDel,
  IdOk,
  IdClose,
  IdReset,
  IdProgress,
  IdNewest,
  IdHosts,
  IdSettings,
  IdProfileBasic,
  IdProfileCommon,
  IdProfileDetailed,
  IdOpenService,
  IdOpenRange,
  IdRangeAuto,
  IdRangeCustom,
  IdCount64,
  IdCount128,
  IdCount256,
  IdWindowPrev,
  IdWindowNext,
  IdAllHosts,
  IdOpenOnly,
  IdKeyBase = 100,
  // Rows used to sit in the sequential list, which made IdRow0 + 1 equal IdPrev.
  // Previous, Next, and Back then never ran. Keep rows in their own range.
  IdRow0 = 200
};

enum class UiPhase : uint8_t { Home, Results, Password, Hosts, Settings };
enum class SettingsPage : uint8_t { Menu = 0, Service = 1, Range = 2, Edit = 3 };

struct UiControl {
  int id;
  int x;
  int y;
  int w;
  int h;
  char label[22];
  char detail[40];
  char vendor[32];
  char note[22];
  char value;
  bool latched;
  bool secondary;
  bool dim;
  bool chrome;
  bool cancel;
};

struct UiSnapshot {
  UiPhase phase = UiPhase::Home;
  bool saved = false;
  bool shift = false;
  int keyboardPage = 0;
  int listPage = 0;
  ScanState scan = ScanState::Idle;
  bool showDashboard = false;
  char progressLabel[22] = {};
  char progressDetail[22] = {};
  char newestLabel[22] = {};
  char newestDetail[40] = {};
  char newestVendor[32] = {};
  ServiceProfile profile = ServiceProfile::Common;
  SettingsPage settingsPage = SettingsPage::Menu;
  bool rangeAutomatic = true;
  bool rangeCanPrev = false;
  bool rangeCanNext = false;
  uint16_t rangeLimit = 256;
  char rangeStart[16] = {};
  char rangeEnd[16] = {};
  char rangeNote[22] = {};
  char editText[16] = {};
  bool rowPresent[6] = {};
  bool rowSelected[6] = {};
  char rowLabel[6][22] = {};
  char rowDetail[6][40] = {};
  char rowVendor[6][32] = {};
  char rowNote[6][22] = {};
  bool openOnly = false;
  bool hostDetail = false;
  int detailPage = 0;
  int visibleCount = 0;
  char detailTitle[16] = {};
  char detailName[22] = {};
  char emptyNote[32] = {};
};

struct UiGesture {
  int hitId = -1;
  bool fire = false;
  bool cancelled = false;
  int shownAtSample = -1;
  ControlFace faceWhileDown = ControlFace::Normal;
};

const char* uiControlName(int id);
int collectUiControls(UiControl* out, int cap, const UiSnapshot& snapshot);
// One reusable list. Callers must not nest two uses.
UiControl* uiScratchControls(int* cap);
int hitUiControl(const UiControl* controls, int count, int x, int y);
bool alphabetCaseIs(const UiSnapshot& snapshot, bool upper);
UiGesture playTap(const UiSnapshot& snapshot, int x, int y, uint32_t upMs, uint32_t sampleMs);
UiGesture playDrag(const UiSnapshot& snapshot, int x0, int y0, int x1, int y1, uint32_t moveMs, uint32_t upMs);
