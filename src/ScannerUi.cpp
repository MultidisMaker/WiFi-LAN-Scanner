#include "ScannerUi.h"

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "AppActions.h"
#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "InventoryStore.h"
#include "NameRecord.h"
#include "NetMath.h"
#include "Oui.h"
#include "NetworkRange.h"
#include "TouchBoard.h"
#include "UiModel.h"
#include "UiStatus.h"

namespace {

WifiService* wifiFrom(void* context) { return static_cast<WifiService*>(context); }

void hookFind(void* context) { wifiFrom(context)->requestScan(); }

void hookForget(void* context) { wifiFrom(context)->forget(); }

void hookSelect(void* context, int index) { wifiFrom(context)->selectResult(index); }

void hookShift(void* context) { wifiFrom(context)->toggleShift(); }

void hookBackspace(void* context) { wifiFrom(context)->backspace(); }

void hookSubmit(void* context) { wifiFrom(context)->submitPassword(); }

void hookCancel(void* context) { wifiFrom(context)->cancelPassword(); }

const char* phaseToken(WifiPhase phase) {
  switch (phase) {
    case WifiPhase::Idle:
      return "idle";
    case WifiPhase::Scanning:
      return "scanning";
    case WifiPhase::Results:
      return "results";
    case WifiPhase::Password:
      return "entry";
    case WifiPhase::Connecting:
      return "connecting";
    case WifiPhase::Connected:
      return "connected";
    case WifiPhase::Failed:
      return "failed";
  }
  return "unknown";
}

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

int gatherControls(UiControl* out, int cap, const WifiService& wifi, const ScannerController& scanner, int page,
                   int keyboardPage, bool hostsView) {
  UiSnapshot snapshot;
  const WifiPhase phase = wifi.phase();
  if (phase == WifiPhase::Results) {
    snapshot.phase = UiPhase::Results;
  } else if (phase == WifiPhase::Password) {
    snapshot.phase = UiPhase::Password;
  } else if (hostsView) {
    snapshot.phase = UiPhase::Hosts;
  } else {
    snapshot.phase = UiPhase::Home;
  }
  snapshot.saved = wifi.hasSavedNetwork();
  snapshot.shift = wifi.shiftOn();
  snapshot.keyboardPage = keyboardPage;
  snapshot.listPage = page;
  snapshot.scan = scanner.state();
  if (snapshot.phase == UiPhase::Results) {
    const int start = page * 6;
    for (int row = 0; row < 6; ++row) {
      const WifiAp* ap = wifi.resultAt(start + row);
      if (ap == nullptr) {
        continue;
      }
      snapshot.rowPresent[row] = true;
      copyLabel(snapshot.rowLabel[row], sizeof(snapshot.rowLabel[row]), ap->ssid);
      snprintf(snapshot.rowDetail[row], sizeof(snapshot.rowDetail[row]), "%s %ld dBm", securityLabel(ap->secure),
               static_cast<long>(ap->rssi));
    }
  } else if (snapshot.phase == UiPhase::Hosts) {
    const int start = page * 6;
    for (int row = 0; row < 6; ++row) {
      const ObservedHost* host = scanner.hostAt(static_cast<uint16_t>(start + row));
      if (host == nullptr) {
        continue;
      }
      snapshot.rowPresent[row] = true;
      formatIpv4(host->ip, snapshot.rowLabel[row], sizeof(snapshot.rowLabel[row]));
      formatHostDetail(snapshot.rowDetail[row], sizeof(snapshot.rowDetail[row]), host->nameSource, host->name,
                       host->hasMac, host->mac);
      formatOuiLine(snapshot.rowVendor[row], sizeof(snapshot.rowVendor[row]), host->ouiState, host->manufacturer);
    }
  } else if (snapshot.phase == UiPhase::Home) {
    snapshot.showDashboard = true;
    formatScanCard(snapshot.progressLabel, sizeof(snapshot.progressLabel), scanner.state(),
                   wifi.phase() == WifiPhase::Connected, scanner.processedCount(), scanner.candidateCount());
    const unsigned long seconds = static_cast<unsigned long>(scanner.elapsedMs() / 1000UL);
    if (scanner.hasCurrent()) {
      char current[16];
      formatIpv4(scanner.currentAddress(), current, sizeof(current));
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "%s %lus", current, seconds);
    } else if (scanner.hasLast()) {
      char last[16];
      formatIpv4(scanner.lastAddress(), last, sizeof(last));
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "last %s", last);
    } else {
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "elapsed %lus", seconds);
    }
    const ObservedHost* newest = scanner.newest();
    if (newest == nullptr) {
      copyLabel(snapshot.newestLabel, sizeof(snapshot.newestLabel), "No device yet");
      copyLabel(snapshot.newestDetail, sizeof(snapshot.newestDetail), "none observed");
    } else {
      formatIpv4(newest->ip, snapshot.newestLabel, sizeof(snapshot.newestLabel));
      formatHostDetail(snapshot.newestDetail, sizeof(snapshot.newestDetail), newest->nameSource, newest->name,
                       newest->hasMac, newest->mac);
      formatOuiLine(snapshot.newestVendor, sizeof(snapshot.newestVendor), newest->ouiState, newest->manufacturer);
    }
  }
  return collectUiControls(out, cap, snapshot);
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
  if (control.vendor[0] != '\0' && control.h >= 44) {
    textLine(gfx, control.x + 6, control.y + 36, 1, ink, control.vendor);
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
  if (showingHosts_ && wifi_->phase() != WifiPhase::Results && wifi_->phase() != WifiPhase::Password) {
    textLine(gfx, 8, 8, 2, CYAN, "Hosts");
    char line[40];
    snprintf(line, sizeof(line), "Observed %u via ARP", scanner_->observedCount());
    textLine(gfx, 8, 28, 1, WHITE, line);
    char panel[40];
    if (formatPersistPanel(panel, sizeof(panel), lastInventoryStore())) {
      const InventoryStoreStatus status = lastInventoryStore().status;
      const uint16_t ink = status == InventoryStoreStatus::Stored ? GREEN
                           : status == InventoryStoreStatus::Failed ? RED
                           : status == InventoryStoreStatus::Absent ? ORANGE
                                                                    : DARKGREY;
      textLine(gfx, 8, 462, 1, ink, panel);
    }
    return;
  }
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

  textLine(gfx, 8, 8, 2, CYAN, "WiFi LAN");
  char card[24];
  if (formatScanCard(card, sizeof(card), scanner_->state(), wifi_->phase() == WifiPhase::Connected,
                     scanner_->processedCount(), scanner_->candidateCount())) {
    textLine(gfx, 8, 26, 1, CYAN, card);
  }
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
    snprintf(line, sizeof(line), "SSID %.18s", WiFi.SSID().c_str());
    textLine(gfx, 8, 146, 1, WHITE, line);
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
    char banner[64];
    if (formatScanBanner(banner, sizeof(banner), scanner_->state(), false, scanner_->processedCount(),
                         scanner_->candidateCount(), scanner_->observedCount())) {
      textLine(gfx, 8, 160, 1, DARKGREY, banner);
    }
  }

  char panel[40];
  if (formatPersistPanel(panel, sizeof(panel), lastInventoryStore())) {
    const InventoryStoreStatus status = lastInventoryStore().status;
    const uint16_t ink = status == InventoryStoreStatus::Stored ? GREEN
                         : status == InventoryStoreStatus::Failed ? RED
                         : status == InventoryStoreStatus::Absent ? ORANGE
                                                                  : DARKGREY;
    textLine(gfx, 8, 462, 1, ink, panel);
  }
  if (!deviceTouch().ready()) {
    textLine(gfx, 8, 472, 1, RED, "Touch controller absent");
  }
}

void ScannerUi::paintControls() {
  if (!deviceDisplay().ready() || wifi_ == nullptr || scanner_ == nullptr) {
    return;
  }
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = gatherControls(controls, cap, *wifi_, *scanner_, page_, keyboardPage_, showingHosts_);
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
  drawnObserved_ = scanner_->observedCount();
  drawnProcessed_ = scanner_->processedCount();
  drawnHosts_ = showingHosts_;
  drawnStore_ = lastInventoryStore().status;
  snprintf(drawnPath_, sizeof(drawnPath_), "%s", lastInventoryStore().path);
  lastDrawMs_ = millis();
  force_ = false;
}

int ScannerUi::hitControl(int x, int y) const {
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = gatherControls(controls, cap, *wifi_, *scanner_, page_, keyboardPage_, showingHosts_);
  return hitUiControl(controls, count, x, y);
}

void ScannerUi::dispatch(int id) {
  if (id >= IdKeyBase && id < IdRow0) {
    int cap = 0;
    UiControl* controls = uiScratchControls(&cap);
    const int count = gatherControls(controls, cap, *wifi_, *scanner_, page_, keyboardPage_, showingHosts_);
    for (int i = 0; i < count; ++i) {
      if (controls[i].id == id && controls[i].value != 0) {
        wifi_->typeChar(controls[i].value);
        return;
      }
    }
    return;
  }

  int row = -1;
  const AppAction action = actionFromControl(id, &row);
  if (action == AppAction::None) {
    return;
  }
  applyRemote(action, row);
}

bool ScannerUi::applyRemote(AppAction action, int rowOffset) {
  if (wifi_ == nullptr || scanner_ == nullptr || action == AppAction::None) {
    return false;
  }
  if (action == AppAction::SelectRow && (rowOffset < 0 || rowOffset > 5)) {
    return false;
  }
  AppView view;
  view.showingHosts = showingHosts_;
  view.resultsOpen = !showingHosts_ && wifi_->phase() == WifiPhase::Results;
  view.page = page_;
  view.keyboardPage = keyboardPage_;
  view.resultCount = wifi_->resultCount();
  view.observedCount = scanner_->observedCount();
  view.rowOffset = rowOffset;
  AppHooks hooks;
  hooks.findNetworks = hookFind;
  hooks.forgetNetwork = hookForget;
  hooks.selectResult = hookSelect;
  hooks.toggleShift = hookShift;
  hooks.backspace = hookBackspace;
  hooks.submitPassword = hookSubmit;
  hooks.cancelPassword = hookCancel;
  hooks.context = wifi_;
  applyAppAction(action, view, *scanner_, &hooks);
  showingHosts_ = view.showingHosts;
  page_ = view.page;
  keyboardPage_ = view.keyboardPage;
  force_ = true;
  return true;
}

void ScannerUi::captureState(AppState& out) const {
  if (wifi_ == nullptr || scanner_ == nullptr) {
    out = AppState();
    return;
  }
  AppView view;
  view.showingHosts = showingHosts_;
  view.resultsOpen = !showingHosts_ && wifi_->phase() == WifiPhase::Results;
  view.page = page_;
  view.keyboardPage = keyboardPage_;
  view.resultCount = wifi_->resultCount();
  view.observedCount = scanner_->observedCount();
  AppWifiView wifi;
  wifi.phase = phaseToken(wifi_->phase());
  const char* ssid = wifi_->hasSavedNetwork() ? wifi_->savedSsid() : wifi_->selectedSsid();
  wifi.ssid = ssid == nullptr ? "" : ssid;
  wifi.saved = wifi_->hasSavedNetwork();
  wifi.shift = wifi_->shiftOn();
  wifi.entry = wifi_->phase() == WifiPhase::Password;
  wifi.results = wifi_->phase() == WifiPhase::Results;
  fillAppState(out, view, *scanner_, wifi);
}

void ScannerUi::noteTouch(const char* event, int id, int x, int y, bool includePoint) {
  const bool password = wifi_->phase() == WifiPhase::Password || (id >= IdKeyBase && id < IdRow0);
  if (password) {
    Serial.printf("WLS touch %s\n", event);
    return;
  }
  if (includePoint) {
    Serial.printf("WLS touch %s control=%s x=%d y=%d\n", event, uiControlName(id), x, y);
  } else {
    Serial.printf("WLS touch %s control=%s\n", event, uiControlName(id));
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

  const InventoryStoreResult& storedNow = lastInventoryStore();
  const bool storeChanged = storedNow.status != drawnStore_ || strcmp(storedNow.path, drawnPath_) != 0;
  const bool contentChanged = force_ || storeChanged || wifi_->phase() != drawnPhase_ || scanner_->state() != drawnScan_ ||
                              wifi_->shiftOn() != drawnShift_ || wifi_->passwordLength() != drawnPassLen_ ||
                              wifi_->hasSavedNetwork() != drawnSaved_ || page_ != drawnPage_ ||
                              keyboardPage_ != drawnKeyboard_ || showingHosts_ != drawnHosts_ ||
                              scanner_->observedCount() != drawnObserved_ ||
                              scanner_->processedCount() != drawnProcessed_;
  const bool pulse = (wifi_->phase() == WifiPhase::Connecting || wifi_->phase() == WifiPhase::Scanning ||
                      scanner_->state() == ScanState::Scanning) &&
                     millis() - lastDrawMs_ > 800;
  if (contentChanged || pulse) {
    draw(true);
  } else if (step.visualChanged) {
    paintControls();
  }
}
