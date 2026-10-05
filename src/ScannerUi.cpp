#include "ScannerUi.h"

#include <Arduino_GFX_Library.h>
#include <stdio.h>
#include <string.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "NetworkRange.h"
#include "TouchBoard.h"

namespace {

enum ControlId : int {
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
  IdKeyBase = 100
};

struct UiControl {
  int id;
  int x;
  int y;
  int w;
  int h;
  char label[22];
  char detail[22];
  char value;
  bool latched;
};

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

void textLine(Arduino_GFX& gfx, int x, int y, int size, uint16_t color, const char* text) {
  gfx.setTextSize(size);
  gfx.setTextColor(color);
  gfx.setCursor(x, y);
  gfx.print(text == nullptr ? "" : text);
}

const char* securityLabel(bool secure) { return secure ? "SEC" : "OPEN"; }

const char* controlName(int id) {
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

int appendControl(UiControl* out, int count, int cap, int id, int x, int y, int w, int h, const char* label,
                  const char* detail, char value, bool latched) {
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
  control.value = value;
  control.latched = latched;
  return count + 1;
}

const char* keyboardRows(int page, int row) {
  static const char* pages[2][5] = {
      {"abcdef", "ghijkl", "mnopqr", "stuvw", "xyz"},
      {"012345", "6789", "-_@.", "/", ""},
  };
  return pages[page & 1][row];
}

int collectControls(UiControl* out, int cap, WifiPhase phase, bool saved, bool shift, int keyboardPage, int page,
                    int resultCount, ScanState scan, const WifiService& wifi) {
  int count = 0;
  if (phase == WifiPhase::Results) {
    const int pageSize = 6;
    const int start = page * pageSize;
    for (int row = 0; row < pageSize; ++row) {
      const WifiAp* ap = wifi.resultAt(start + row);
      if (ap == nullptr) {
        continue;
      }
      char detail[22];
      snprintf(detail, sizeof(detail), "%s %ld dBm", securityLabel(ap->secure), static_cast<long>(ap->rssi));
      count = appendControl(out, count, cap, IdRow0 + row, 6, 40 + row * 52, 210, 48, ap->ssid, detail, 0, false);
    }
    count = appendControl(out, count, cap, IdPrev, 6, 430, 64, 40, "Prev", nullptr, 0, false);
    count = appendControl(out, count, cap, IdNext, 76, 430, 64, 40, "Next", nullptr, 0, false);
    count = appendControl(out, count, cap, IdBack, 146, 430, 70, 40, "Back", nullptr, 0, false);
    return count;
  }

  if (phase == WifiPhase::Password) {
    int keyIndex = 0;
    for (int row = 0; row < 5; ++row) {
      const char* keys = keyboardRows(keyboardPage, row);
      for (int col = 0; keys[col] != '\0'; ++col, ++keyIndex) {
        const char glyph = keyGlyph(keys[col], shift);
        char label[2] = {glyph, '\0'};
        count = appendControl(out, count, cap, IdKeyBase + keyIndex, 6 + col * 36, 96 + row * 46, 34, 42, label,
                              nullptr, glyph, false);
      }
    }
    count = appendControl(out, count, cap, IdShift, 6, 430, 50, 40, shift ? "SHIFT" : "shift", nullptr, 0, shift);
    count = appendControl(out, count, cap, IdPage, 60, 430, 40, 40, "Pg", nullptr, 0, false);
    count = appendControl(out, count, cap, IdDel, 104, 430, 36, 40, "Del", nullptr, 0, false);
    count = appendControl(out, count, cap, IdOk, 144, 430, 34, 40, "OK", nullptr, 0, false);
    count = appendControl(out, count, cap, IdClose, 182, 430, 34, 40, "X", nullptr, 0, false);
    return count;
  }

  count = appendControl(out, count, cap, IdFind, 8, 72, 206, 34, "Find networks", nullptr, 0, false);
  if (saved) {
    count = appendControl(out, count, cap, IdForget, 8, 112, 206, 34, "Forget network", nullptr, 0, false);
  }
  count = appendControl(out, count, cap, IdStart, 8, 328, 100, 40, scan == ScanState::Complete ? "Reset" : "Start",
                        nullptr, 0, false);
  count = appendControl(out, count, cap, IdPause, 114, 328, 100, 40, "Pause", nullptr, 0, false);
  count = appendControl(out, count, cap, IdResume, 8, 376, 100, 40, "Resume", nullptr, 0, false);
  count = appendControl(out, count, cap, IdStop, 114, 376, 100, 40, "Stop", nullptr, 0, false);
  (void)resultCount;
  return count;
}

void paintControl(Arduino_GFX& gfx, const UiControl& control, bool pressed) {
  const ControlFace face = controlFace(control.latched, pressed);
  uint16_t fill = BLACK;
  uint16_t ink = WHITE;
  uint16_t border = CYAN;
  if (face == ControlFace::Pressed) {
    fill = WHITE;
    ink = BLACK;
    border = WHITE;
  } else if (face == ControlFace::Latched) {
    fill = CYAN;
    ink = BLACK;
    border = CYAN;
  } else if (face == ControlFace::LatchedPressed) {
    fill = ORANGE;
    ink = BLACK;
    border = ORANGE;
  }
  gfx.fillRect(control.x, control.y, control.w, control.h, fill);
  gfx.drawRect(control.x, control.y, control.w, control.h, border);
  const bool single = control.label[0] != '\0' && control.label[1] == '\0';
  const int size = single ? 2 : 1;
  const int textW = single ? 12 : 6 * static_cast<int>(strlen(control.label));
  int textX = control.x + 6;
  if (single) {
    textX = control.x + (control.w - textW) / 2;
    if (textX < control.x + 1) {
      textX = control.x + 1;
    }
  }
  const int textY = control.detail[0] == '\0' ? control.y + (control.h / 2) - (single ? 8 : 4) : control.y + 8;
  textLine(gfx, textX, textY, size, ink, control.label);
  if (control.detail[0] != '\0') {
    textLine(gfx, control.x + 6, control.y + 26, 1, ink, control.detail);
  }
}

}  // namespace

void ScannerUi::begin(WifiService& wifi, ScannerController& scanner) {
  wifi_ = &wifi;
  scanner_ = &scanner;
  force_ = true;
}

void ScannerUi::drawChrome() {
  Arduino_GFX& gfx = deviceDisplay().panel();
  if (wifi_->phase() == WifiPhase::Results) {
    textLine(gfx, 8, 8, 2, CYAN, "Networks");
    return;
  }
  if (wifi_->phase() == WifiPhase::Password) {
    textLine(gfx, 8, 8, 1, CYAN, "Password");
    char ssidLine[40];
    snprintf(ssidLine, sizeof(ssidLine), "%.20s", wifi_->selectedSsid());
    textLine(gfx, 8, 24, 1, WHITE, ssidLine);
    char mask[kMaxPassLen + 1];
    maskPassword(mask, sizeof(mask), wifi_->passwordLength());
    textLine(gfx, 8, 44, 2, WHITE, wifi_->passwordLength() == 0 ? "(empty)" : mask);
    textLine(gfx, 8, 72, 1, wifi_->shiftOn() ? CYAN : DARKGREY, wifi_->shiftOn() ? "ABC" : "abc");
    return;
  }

  textLine(gfx, 8, 8, 2, CYAN, "LAN Scanner");
  textLine(gfx, 8, 36, 1, WHITE, wifi_->statusText());
  if (wifi_->hasSavedNetwork()) {
    char line[40];
    snprintf(line, sizeof(line), "Saved %.18s", wifi_->savedSsid());
    textLine(gfx, 8, 52, 1, WHITE, line);
  } else {
    textLine(gfx, 8, 52, 1, WHITE, "No saved network");
  }

  char line[48];
  if (wifi_->phase() == WifiPhase::Connected) {
    const NetworkRange range = rangeFromStation();
    char ip[16];
    char mask[16];
    char gateway[16];
    char dns[16];
    char network[16];
    formatIp(range.address, ip, sizeof(ip));
    formatIp(range.mask, mask, sizeof(mask));
    formatIp(range.gateway, gateway, sizeof(gateway));
    formatIp(range.dnsPrimary, dns, sizeof(dns));
    formatIp(range.network, network, sizeof(network));
    snprintf(line, sizeof(line), "IP %s", ip);
    textLine(gfx, 8, 160, 1, WHITE, line);
    snprintf(line, sizeof(line), "Mask %s", mask);
    textLine(gfx, 8, 174, 1, WHITE, line);
    snprintf(line, sizeof(line), "GW %s", gateway);
    textLine(gfx, 8, 188, 1, WHITE, line);
    snprintf(line, sizeof(line), "DNS %s", dns);
    textLine(gfx, 8, 202, 1, WHITE, line);
    if (range.valid) {
      snprintf(line, sizeof(line), "Net %s/%u", network, range.prefix);
      textLine(gfx, 8, 216, 1, GREEN, line);
      snprintf(line, sizeof(line), "Usable %lu cap %lu", static_cast<unsigned long>(range.usableHosts),
               static_cast<unsigned long>(range.futureScanCap));
      textLine(gfx, 8, 230, 1, GREEN, line);
    } else {
      textLine(gfx, 8, 216, 1, ORANGE, "Range unavailable");
    }
  } else {
    textLine(gfx, 8, 160, 1, DARKGREY, "Range waits for Wi-Fi");
  }

  snprintf(line, sizeof(line), "Scanner %s", scanStateName(scanner_->state()));
  textLine(gfx, 8, 260, 2, WHITE, line);
  textLine(gfx, 8, 286, 1, ORANGE, "Host discovery deferred");
  textLine(gfx, 8, 302, 1, DARKGREY, "No probes are sent");
  if (!deviceTouch().ready()) {
    textLine(gfx, 8, 430, 1, RED, "Touch controller absent");
  }
}

void ScannerUi::paintControls() {
  if (!deviceDisplay().ready() || wifi_ == nullptr || scanner_ == nullptr) {
    return;
  }
  UiControl controls[40];
  const int count = collectControls(controls, 40, wifi_->phase(), wifi_->hasSavedNetwork(), wifi_->shiftOn(),
                                    keyboardPage_, page_, wifi_->resultCount(), scanner_->state(), *wifi_);
  Arduino_GFX& gfx = deviceDisplay().panel();
  const int shown = press_.shownId();
  for (int i = 0; i < count; ++i) {
    paintControl(gfx, controls[i], controls[i].id == shown);
  }
}

void ScannerUi::draw(bool full) {
  if (!deviceDisplay().ready()) {
    return;
  }
  if (full) {
    deviceDisplay().panel().fillScreen(BLACK);
  }
  drawChrome();
  paintControls();
  drawnPhase_ = wifi_->phase();
  drawnScan_ = scanner_->state();
  drawnShift_ = wifi_->shiftOn();
  drawnPassLen_ = wifi_->passwordLength();
  drawnSaved_ = wifi_->hasSavedNetwork();
  drawnPage_ = page_;
  drawnKeyboard_ = keyboardPage_;
  lastDrawMs_ = millis();
  force_ = false;
}

int ScannerUi::hitControl(int x, int y) const {
  UiControl controls[40];
  const int count = collectControls(controls, 40, wifi_->phase(), wifi_->hasSavedNetwork(), wifi_->shiftOn(),
                                    keyboardPage_, page_, wifi_->resultCount(), scanner_->state(), *wifi_);
  for (int i = 0; i < count; ++i) {
    const UiControl& control = controls[i];
    if (x >= control.x && x < control.x + control.w && y >= control.y && y < control.y + control.h) {
      return control.id;
    }
  }
  return -1;
}

void ScannerUi::dispatch(int id) {
  if (id >= IdKeyBase) {
    UiControl controls[40];
    const int count = collectControls(controls, 40, wifi_->phase(), wifi_->hasSavedNetwork(), wifi_->shiftOn(),
                                      keyboardPage_, page_, wifi_->resultCount(), scanner_->state(), *wifi_);
    for (int i = 0; i < count; ++i) {
      if (controls[i].id == id && controls[i].value != 0) {
        wifi_->typeChar(controls[i].value);
        return;
      }
    }
    return;
  }

  if (id >= IdRow0 && id < IdRow0 + 6) {
    wifi_->selectResult(page_ * 6 + (id - IdRow0));
    return;
  }

  switch (id) {
    case IdFind:
      page_ = 0;
      wifi_->requestScan();
      break;
    case IdForget:
      wifi_->forget();
      break;
    case IdStart:
      if (scanner_->state() == ScanState::Complete) {
        scanner_->acknowledge();
      } else {
        scanner_->start();
      }
      break;
    case IdPause:
      scanner_->pause();
      break;
    case IdResume:
      scanner_->resume();
      break;
    case IdStop:
      scanner_->stop();
      break;
    case IdPrev:
      if (page_ > 0) {
        --page_;
      }
      break;
    case IdNext:
      if ((page_ + 1) * 6 < wifi_->resultCount()) {
        ++page_;
      }
      break;
    case IdBack:
      page_ = 0;
      wifi_->cancelPassword();
      break;
    case IdShift:
      wifi_->toggleShift();
      break;
    case IdPage:
      keyboardPage_ ^= 1;
      break;
    case IdDel:
      wifi_->backspace();
      break;
    case IdOk:
      wifi_->submitPassword();
      break;
    case IdClose:
      wifi_->cancelPassword();
      break;
    default:
      break;
  }
}

void ScannerUi::noteTouch(const char* event, int id, int x, int y, bool includePoint) {
  const bool password = wifi_->phase() == WifiPhase::Password || id >= IdKeyBase;
  if (password) {
    Serial.printf("WLS touch %s\n", event);
    return;
  }
  if (includePoint) {
    Serial.printf("WLS touch %s control=%s x=%d y=%d\n", event, controlName(id), x, y);
  } else {
    Serial.printf("WLS touch %s control=%s\n", event, controlName(id));
  }
}

void ScannerUi::loop() {
  if (wifi_ == nullptr || scanner_ == nullptr) {
    return;
  }
  bool down = false;
  int x = 0;
  int y = 0;
  deviceTouch().readContact(down, x, y);
  const int hit = down ? hitControl(x, y) : -1;
  const PressStep step = press_.update(down, hit, millis());
  if (step.began) {
    noteTouch("down", step.beganId, x, y, true);
  }
  if (step.cancelled) {
    noteTouch("cancel", step.cancelId, x, y, false);
  }
  if (step.fire) {
    noteTouch("up", step.fireId, x, y, false);
    dispatch(step.fireId);
    force_ = true;
  }

  const bool contentChanged = force_ || wifi_->phase() != drawnPhase_ || scanner_->state() != drawnScan_ ||
                              wifi_->shiftOn() != drawnShift_ || wifi_->passwordLength() != drawnPassLen_ ||
                              wifi_->hasSavedNetwork() != drawnSaved_ || page_ != drawnPage_ ||
                              keyboardPage_ != drawnKeyboard_;
  const bool pulse = (wifi_->phase() == WifiPhase::Connecting || wifi_->phase() == WifiPhase::Scanning) &&
                     millis() - lastDrawMs_ > 800;
  if (contentChanged || pulse) {
    draw(true);
  } else if (step.visualChanged) {
    paintControls();
  }
}
