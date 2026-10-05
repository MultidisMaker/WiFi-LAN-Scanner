#include "ScannerUi.h"

#include <Arduino_GFX_Library.h>
#include <ctype.h>
#include <stdio.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "NetworkRange.h"
#include "TouchBoard.h"

namespace {
void textLine(Arduino_GFX& gfx, int x, int y, int size, uint16_t color, const char* text) {
  gfx.setTextSize(size);
  gfx.setTextColor(color);
  gfx.setCursor(x, y);
  gfx.print(text == nullptr ? "" : text);
}

const char* securityLabel(bool secure) { return secure ? "SEC" : "OPEN"; }
}

void ScannerUi::begin(WifiService& wifi, ScannerController& scanner) {
  wifi_ = &wifi;
  scanner_ = &scanner;
  force_ = true;
}

bool ScannerUi::hit(int x, int y, int bx, int by, int bw, int bh) const {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

void ScannerUi::button(int x, int y, int w, int h, const char* label) const {
  Arduino_GFX& gfx = deviceDisplay().panel();
  gfx.drawRect(x, y, w, h, CYAN);
  textLine(gfx, x + 6, y + (h / 2) - 4, 1, WHITE, label);
}

void ScannerUi::drawHome() {
  Arduino_GFX& gfx = deviceDisplay().panel();
  textLine(gfx, 8, 8, 2, CYAN, "LAN Scanner");
  textLine(gfx, 8, 36, 1, WHITE, wifi_->statusText());
  if (wifi_->hasSavedNetwork()) {
    char line[40];
    snprintf(line, sizeof(line), "Saved %.18s", wifi_->savedSsid());
    textLine(gfx, 8, 52, 1, WHITE, line);
  } else {
    textLine(gfx, 8, 52, 1, WHITE, "No saved network");
  }
  button(8, 72, 206, 34, "Find networks");
  if (wifi_->hasSavedNetwork()) {
    button(8, 112, 206, 34, "Forget network");
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
  button(8, 328, 100, 40, scanner_->state() == ScanState::Complete ? "Reset" : "Start");
  button(114, 328, 100, 40, "Pause");
  button(8, 376, 100, 40, "Resume");
  button(114, 376, 100, 40, "Stop");
  if (!deviceTouch().ready()) {
    textLine(gfx, 8, 430, 1, RED, "Touch controller absent");
  }
}

void ScannerUi::drawResults() {
  Arduino_GFX& gfx = deviceDisplay().panel();
  textLine(gfx, 8, 8, 2, CYAN, "Networks");
  const int pageSize = 6;
  const int start = page_ * pageSize;
  for (int row = 0; row < pageSize; ++row) {
    const WifiAp* ap = wifi_->resultAt(start + row);
    const int y = 40 + row * 52;
    gfx.drawRect(6, y, 210, 48, WHITE);
    if (ap == nullptr) {
      continue;
    }
    char line[40];
    snprintf(line, sizeof(line), "%.20s", ap->ssid);
    textLine(gfx, 12, y + 8, 1, WHITE, line);
    snprintf(line, sizeof(line), "%s %ld dBm", securityLabel(ap->secure), static_cast<long>(ap->rssi));
    textLine(gfx, 12, y + 26, 1, CYAN, line);
  }
  button(6, 430, 64, 40, "Prev");
  button(76, 430, 64, 40, "Next");
  button(146, 430, 70, 40, "Back");
}

void ScannerUi::drawPassword() {
  Arduino_GFX& gfx = deviceDisplay().panel();
  textLine(gfx, 8, 8, 1, CYAN, "Password");
  char ssidLine[40];
  snprintf(ssidLine, sizeof(ssidLine), "%.20s", wifi_->selectedSsid());
  textLine(gfx, 8, 24, 1, WHITE, ssidLine);
  char mask[kMaxPassLen + 1];
  const int length = wifi_->passwordLength();
  for (int i = 0; i < length && i < kMaxPassLen; ++i) {
    mask[i] = '*';
  }
  mask[length] = '\0';
  textLine(gfx, 8, 44, 2, WHITE, length == 0 ? "(empty)" : mask);
  textLine(gfx, 8, 72, 1, wifi_->shiftOn() ? CYAN : DARKGREY, wifi_->shiftOn() ? "Shift on" : "Shift off");

  static const char* pages[2][5] = {
      {"abcdef", "ghijkl", "mnopqr", "stuvw", "xyz"},
      {"012345", "6789", "-_@.", "/", ""},
  };
  const int page = keyboardPage_ & 1;
  for (int row = 0; row < 5; ++row) {
    const char* keys = pages[page][row];
    for (int col = 0; keys[col] != '\0'; ++col) {
      char label[2] = {wifi_->shiftOn() ? static_cast<char>(toupper(static_cast<unsigned char>(keys[col]))) : keys[col],
                       '\0'};
      button(6 + col * 36, 96 + row * 46, 34, 42, label);
    }
  }
  button(6, 430, 50, 40, "Shift");
  button(60, 430, 40, 40, "Pg");
  button(104, 430, 36, 40, "Del");
  button(144, 430, 34, 40, "OK");
  button(182, 430, 34, 40, "X");
}

void ScannerUi::draw(bool full) {
  if (!deviceDisplay().ready()) {
    return;
  }
  Arduino_GFX& gfx = deviceDisplay().panel();
  if (full) {
    gfx.fillScreen(BLACK);
  }
  switch (wifi_->phase()) {
    case WifiPhase::Results:
      drawResults();
      break;
    case WifiPhase::Password:
      drawPassword();
      break;
    default:
      drawHome();
      break;
  }
  drawnPhase_ = wifi_->phase();
  drawnScan_ = scanner_->state();
  drawnShift_ = wifi_->shiftOn();
  drawnPassLen_ = wifi_->passwordLength();
  lastDrawMs_ = millis();
  force_ = false;
}

void ScannerUi::handlePress(int x, int y) {
  if (wifi_->phase() == WifiPhase::Results) {
    const int pageSize = 6;
    for (int row = 0; row < pageSize; ++row) {
      const int top = 40 + row * 52;
      if (hit(x, y, 6, top, 210, 48)) {
        wifi_->selectResult(page_ * pageSize + row);
        return;
      }
    }
    if (hit(x, y, 6, 430, 64, 40) && page_ > 0) {
      --page_;
    } else if (hit(x, y, 76, 430, 64, 40)) {
      if ((page_ + 1) * pageSize < wifi_->resultCount()) {
        ++page_;
      }
    } else if (hit(x, y, 146, 430, 70, 40)) {
      page_ = 0;
      wifi_->cancelPassword();
    }
    return;
  }

  if (wifi_->phase() == WifiPhase::Password) {
    static const char* pages[2][5] = {
        {"abcdef", "ghijkl", "mnopqr", "stuvw", "xyz"},
        {"012345", "6789", "-_@.", "/", ""},
    };
    const int page = keyboardPage_ & 1;
    for (int row = 0; row < 5; ++row) {
      const char* keys = pages[page][row];
      for (int col = 0; keys[col] != '\0'; ++col) {
        if (hit(x, y, 6 + col * 36, 96 + row * 46, 34, 42)) {
          char typed = keys[col];
          if (wifi_->shiftOn() && typed >= 'a' && typed <= 'z') {
            typed = static_cast<char>(typed - 'a' + 'A');
          }
          wifi_->typeChar(typed);
          return;
        }
      }
    }
    if (hit(x, y, 6, 430, 50, 40)) {
      wifi_->toggleShift();
    } else if (hit(x, y, 60, 430, 40, 40)) {
      keyboardPage_ ^= 1;
    } else if (hit(x, y, 104, 430, 36, 40)) {
      wifi_->backspace();
    } else if (hit(x, y, 144, 430, 34, 40)) {
      wifi_->submitPassword();
    } else if (hit(x, y, 182, 430, 34, 40)) {
      wifi_->cancelPassword();
    }
    return;
  }

  if (hit(x, y, 8, 72, 206, 34)) {
    page_ = 0;
    wifi_->requestScan();
    return;
  }
  if (wifi_->hasSavedNetwork() && hit(x, y, 8, 112, 206, 34)) {
    wifi_->forget();
    return;
  }
  if (hit(x, y, 8, 328, 100, 40)) {
    if (scanner_->state() == ScanState::Complete) {
      scanner_->acknowledge();
    } else {
      scanner_->start();
    }
  } else if (hit(x, y, 114, 328, 100, 40)) {
    scanner_->pause();
  } else if (hit(x, y, 8, 376, 100, 40)) {
    scanner_->resume();
  } else if (hit(x, y, 114, 376, 100, 40)) {
    scanner_->stop();
  }
}

void ScannerUi::loop() {
  int x = 0;
  int y = 0;
  if (deviceTouch().takePress(x, y)) {
    Serial.printf("WLS touch x=%d y=%d\n", x, y);
    handlePress(x, y);
    force_ = true;
  }
  const bool changed = force_ || wifi_->phase() != drawnPhase_ || scanner_->state() != drawnScan_ ||
                       wifi_->shiftOn() != drawnShift_ || wifi_->passwordLength() != drawnPassLen_;
  const bool pulse = (wifi_->phase() == WifiPhase::Connecting || wifi_->phase() == WifiPhase::Scanning) &&
                     millis() - lastDrawMs_ > 800;
  if (changed || pulse) {
    draw(true);
  }
}
