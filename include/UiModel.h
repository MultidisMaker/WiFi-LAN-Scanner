#pragma once

#include "ScannerController.h"
#include "UiPress.h"

enum UiControlId : int {
  IdFind = 1,
  IdForget,
  IdStart,
  IdPause,
  IdResume,
  IdStop,
  IdRow0,
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
  IdKeyBase = 100
};

enum class UiPhase : uint8_t { Home, Results, Password, Hosts };

struct UiControl {
  int id;
  int x;
  int y;
  int w;
  int h;
  char label[22];
  char detail[40];
  char vendor[32];
  char value;
  bool latched;
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
  bool rowPresent[6] = {};
  char rowLabel[6][22] = {};
  char rowDetail[6][40] = {};
  char rowVendor[6][32] = {};
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
