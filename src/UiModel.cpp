#include "UiModel.h"

#include <stdio.h>
#include <string.h>

namespace {
void copyLabel(char* dest, size_t destLen, const char* text) {
  if (destLen == 0) {
    return;
  }
  size_t i = 0;
  if (text != nullptr) {
    for (; text[i] != '\0' && i + 1 < destLen; ++i) {
      dest[i] = text[i];
    }
  }
  dest[i] = '\0';
}

int appendControl(UiControl* out, int count, int cap, int id, int x, int y, int w, int h, const char* label,
                  const char* detail, char value, bool latched, const char* vendor = nullptr, bool secondary = false,
                  bool dim = false) {
  if (count >= cap) {
    return count;
  }
  UiControl& control = out[count];
  control.id = id;
  control.x = x;
  control.y = y;
  control.w = w;
  control.h = h;
  copyLabel(control.label, sizeof(control.label), label);
  copyLabel(control.detail, sizeof(control.detail), detail);
  copyLabel(control.vendor, sizeof(control.vendor), vendor);
  control.value = value;
  control.latched = latched;
  control.secondary = secondary;
  control.dim = dim;
  return count + 1;
}

const char* keyboardRows(int page, int row) {
  static const char* pages[2][5] = {
      {"abcdef", "ghijkl", "mnopqr", "stuvw", "xyz"},
      {"012345", "6789", "-_@.", "/", ""},
  };
  return pages[page & 1][row];
}

const UiControl* findControl(const UiControl* controls, int count, int id) {
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == id) {
      return &controls[i];
    }
  }
  return nullptr;
}
}

UiControl* uiScratchControls(int* cap) {
  static UiControl controls[40];
  if (cap != nullptr) {
    *cap = 40;
  }
  return controls;
}

const char* uiControlName(int id) {
  switch (id) {
    case IdFind:
      return "find";
    case IdForget:
      return "forget";
    case IdStart:
      return "start";
    case IdPause:
      return "pause";
    case IdResume:
      return "resume";
    case IdStop:
      return "stop";
    case IdPrev:
      return "prev";
    case IdNext:
      return "next";
    case IdBack:
      return "back";
    case IdShift:
      return "shift";
    case IdPage:
      return "page";
    case IdDel:
      return "del";
    case IdOk:
      return "ok";
    case IdClose:
      return "close";
    case IdReset:
      return "reset";
    case IdProgress:
      return "progress";
    case IdNewest:
      return "newest";
    case IdHosts:
      return "hosts";
    case IdSettings:
      return "settings";
    case IdProfileBasic:
      return "basic";
    case IdProfileCommon:
      return "common";
    case IdProfileDetailed:
      return "detailed";
    default:
      if (id >= IdRow0 && id < IdRow0 + 6) {
        return "row";
      }
      if (id >= IdKeyBase) {
        return "key";
      }
      return "none";
  }
}

int collectUiControls(UiControl* out, int cap, const UiSnapshot& snapshot) {
  int count = 0;
  if (snapshot.phase == UiPhase::Results) {
    for (int row = 0; row < 6; ++row) {
      if (!snapshot.rowPresent[row]) {
        continue;
      }
      count = appendControl(out, count, cap, IdRow0 + row, 6, 40 + row * 52, 210, 48, snapshot.rowLabel[row],
                            snapshot.rowDetail[row], 0, false, snapshot.rowVendor[row]);
    }
    count = appendControl(out, count, cap, IdPrev, 6, 430, 64, 40, "Prev", nullptr, 0, false);
    count = appendControl(out, count, cap, IdNext, 76, 430, 64, 40, "Next", nullptr, 0, false);
    count = appendControl(out, count, cap, IdBack, 146, 430, 70, 40, "Back", nullptr, 0, false);
    return count;
  }

  if (snapshot.phase == UiPhase::Hosts) {
    for (int row = 0; row < 6; ++row) {
      if (!snapshot.rowPresent[row]) {
        continue;
      }
      count = appendControl(out, count, cap, IdRow0 + row, 6, 40 + row * 52, 210, 48, snapshot.rowLabel[row],
                            snapshot.rowDetail[row], 0, false, snapshot.rowVendor[row]);
    }
    count = appendControl(out, count, cap, IdPrev, 6, 360, 64, 40, "Prev", nullptr, 0, false);
    count = appendControl(out, count, cap, IdNext, 76, 360, 64, 40, "Next", nullptr, 0, false);
    count = appendControl(out, count, cap, IdReset, 146, 360, 70, 40, "Reset", nullptr, 0, false);
    count = appendControl(out, count, cap, IdBack, 6, 410, 210, 40, "Back", nullptr, 0, false);
    return count;
  }

  if (snapshot.phase == UiPhase::Settings) {
    count = appendControl(out, count, cap, IdProfileBasic, 8, 78, 206, 60, "Basic / fast", nullptr, 0,
                          snapshot.profile == ServiceProfile::Basic);
    count = appendControl(out, count, cap, IdProfileCommon, 8, 168, 206, 60, "Common / recommended", nullptr, 0,
                          snapshot.profile == ServiceProfile::Common);
    count = appendControl(out, count, cap, IdProfileDetailed, 8, 328, 206, 52, "Detailed / slower", nullptr, 0,
                          snapshot.profile == ServiceProfile::Detailed);
    count = appendControl(out, count, cap, IdBack, 6, 410, 210, 40, "Back", nullptr, 0, false);
    return count;
  }

  if (snapshot.phase == UiPhase::Password) {
    int keyIndex = 0;
    for (int row = 0; row < 5; ++row) {
      const char* keys = keyboardRows(snapshot.keyboardPage, row);
      for (int col = 0; keys[col] != '\0'; ++col, ++keyIndex) {
        const char glyph = keyGlyph(keys[col], snapshot.shift);
        char label[2] = {glyph, '\0'};
        count = appendControl(out, count, cap, IdKeyBase + keyIndex, 6 + col * 36, 96 + row * 46, 34, 42, label,
                              nullptr, glyph, false);
      }
    }
    count = appendControl(out, count, cap, IdShift, 6, 430, 50, 40, snapshot.shift ? "SHIFT" : "shift", nullptr, 0,
                          snapshot.shift);
    count = appendControl(out, count, cap, IdPage, 60, 430, 40, 40, "Pg", nullptr, 0, false);
    count = appendControl(out, count, cap, IdDel, 104, 430, 36, 40, "Del", nullptr, 0, false);
    count = appendControl(out, count, cap, IdOk, 144, 430, 34, 40, "OK", nullptr, 0, false);
    count = appendControl(out, count, cap, IdClose, 182, 430, 34, 40, "X", nullptr, 0, false);
    return count;
  }

  count = appendControl(out, count, cap, IdFind, 8, 72, 206, 34, "Find networks", nullptr, 0, false);
  if (snapshot.saved) {
    count = appendControl(out, count, cap, IdForget, 8, 112, 206, 34, "Forget network", nullptr, 0, false);
  }
  if (snapshot.showDashboard) {
    count = appendControl(out, count, cap, IdProgress, 8, 240, 206, 44, snapshot.progressLabel, snapshot.progressDetail,
                          0, false);
    count = appendControl(out, count, cap, IdNewest, 8, 286, 206, 34, snapshot.newestLabel, snapshot.newestDetail, 0,
                          false, snapshot.newestVendor);
  }
  const bool scanning = snapshot.scan == ScanState::Scanning;
  const bool paused = snapshot.scan == ScanState::Paused;
  const bool stopping = snapshot.scan == ScanState::Starting || snapshot.scan == ScanState::Stopping;
  if (scanning) {
    count = appendControl(out, count, cap, IdPause, 8, 328, 100, 40, "Pause", nullptr, 0, false);
    count = appendControl(out, count, cap, IdStop, 114, 328, 100, 40, "Stop", nullptr, 0, false);
    count = appendControl(out, count, cap, IdHosts, 8, 376, 100, 40, "Hosts", nullptr, 0, false, nullptr, true, false);
  } else if (paused) {
    count = appendControl(out, count, cap, IdResume, 8, 328, 100, 40, "Resume", nullptr, 0, false);
    count = appendControl(out, count, cap, IdStop, 114, 328, 100, 40, "Stop", nullptr, 0, false);
    count = appendControl(out, count, cap, IdHosts, 8, 376, 100, 40, "Hosts", nullptr, 0, false, nullptr, true, false);
  } else if (stopping) {
    count = appendControl(out, count, cap, IdStop, 8, 328, 100, 40, "Stop", nullptr, 0, false);
    count = appendControl(out, count, cap, IdHosts, 114, 328, 100, 40, "Hosts", nullptr, 0, false, nullptr, true, false);
  } else {
    count = appendControl(out, count, cap, IdStart, 8, 328, 100, 40, "Start", nullptr, 0, false);
    count = appendControl(out, count, cap, IdSettings, 114, 328, 100, 40, "Settings", nullptr, 0, false, nullptr, true,
                          false);
    count = appendControl(out, count, cap, IdHosts, 8, 376, 100, 40, "Hosts", nullptr, 0, false, nullptr, true, false);
    count = appendControl(out, count, cap, IdReset, 114, 376, 100, 40, "Reset", nullptr, 0, false, nullptr, false, true);
  }
  return count;
}

int hitUiControl(const UiControl* controls, int count, int x, int y) {
  for (int i = 0; i < count; ++i) {
    const UiControl& control = controls[i];
    if (x >= control.x && x < control.x + control.w && y >= control.y && y < control.y + control.h) {
      return control.id;
    }
  }
  return -1;
}

bool alphabetCaseIs(const UiSnapshot& snapshot, bool upper) {
  UiSnapshot page = snapshot;
  page.phase = UiPhase::Password;
  page.keyboardPage = 0;
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, page);
  const char* lower = "abcdefghijklmnopqrstuvwxyz";
  int seen = 0;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id < IdKeyBase || controls[i].label[1] != '\0') {
      continue;
    }
    const char expected = upper ? static_cast<char>(lower[seen] - 'a' + 'A') : lower[seen];
    if (controls[i].label[0] != expected) {
      return false;
    }
    ++seen;
  }
  return seen == 26;
}

UiGesture playTap(const UiSnapshot& snapshot, int x, int y, uint32_t upMs, uint32_t sampleMs) {
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, snapshot);
  const int hit = hitUiControl(controls, count, x, y);
  PressTracker tracker;
  UiGesture gesture;
  gesture.hitId = hit;
  const PressStep down = tracker.update(true, hit, 0);
  const UiControl* control = findControl(controls, count, hit);
  gesture.faceWhileDown = controlFace(control != nullptr && control->latched, down.began && tracker.shownId() == hit);
  const PressStep up = tracker.update(false, hit, upMs);
  gesture.fire = up.fire && up.fireId == hit;
  gesture.cancelled = up.cancelled;
  const PressStep sample = tracker.update(false, -1, sampleMs);
  gesture.shownAtSample = sample.shownId;
  (void)down;
  return gesture;
}

UiGesture playDrag(const UiSnapshot& snapshot, int x0, int y0, int x1, int y1, uint32_t moveMs, uint32_t upMs) {
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, snapshot);
  const int first = hitUiControl(controls, count, x0, y0);
  const int second = hitUiControl(controls, count, x1, y1);
  PressTracker tracker;
  UiGesture gesture;
  gesture.hitId = first;
  tracker.update(true, first, 0);
  const PressStep moved = tracker.update(true, second, moveMs);
  const PressStep up = tracker.update(false, second, upMs);
  gesture.fire = up.fire;
  gesture.cancelled = moved.cancelled || up.cancelled;
  gesture.shownAtSample = up.shownId;
  gesture.faceWhileDown = controlFace(false, false);
  return gesture;
}
