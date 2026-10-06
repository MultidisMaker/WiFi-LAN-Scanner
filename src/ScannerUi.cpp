#include "ScannerUi.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <canvas/Arduino_Canvas.h>
#include <stdio.h>
#include <string.h>

#include "ActionAck.h"
#include "AddressRangeStore.h"
#include "AppActions.h"
#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "InventoryStore.h"
#include "NameRecord.h"
#include "NetMath.h"
#include "NetworkRange.h"
#include "Oui.h"
#include "ResourceMeter.h"
#include "ServiceProfileStore.h"
#include "ServiceResultView.h"
#include "ServiceScan.h"
#include "TouchBoard.h"
#include "UiModel.h"
#include "UiRender.h"
#include "UiStatus.h"

namespace {

Arduino_Canvas* gSprite = nullptr;

WifiService* wifiFrom(void* context) { return static_cast<WifiService*>(context); }

void hookFind(void* context) { wifiFrom(context)->requestScan(); }

void hookForget(void* context) { wifiFrom(context)->forget(); }

void hookSelect(void* context, int index) { wifiFrom(context)->selectResult(index); }

void hookShift(void* context) { wifiFrom(context)->toggleShift(); }

void hookBackspace(void* context) { wifiFrom(context)->backspace(); }

void hookSubmit(void* context) { wifiFrom(context)->submitPassword(); }

void hookCancel(void* context) { wifiFrom(context)->cancelPassword(); }

void hookClose(void* context) { wifiFrom(context)->closeResults(); }

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

uint32_t mixText(uint32_t hash, const char* text) {
  if (text == nullptr) {
    return hash;
  }
  for (size_t i = 0; text[i] != '\0'; ++i) {
    hash ^= static_cast<unsigned char>(text[i]);
    hash *= 16777619u;
  }
  return hash;
}

struct Clip {
  Arduino_GFX* gfx;
  int ox;
  int oy;
  int x0;
  int y0;
  int x1;
  int y1;
};

bool boxInside(const Clip& clip, int x, int y, int w, int h) {
  return w >= 0 && h >= 0 && x >= clip.x0 && y >= clip.y0 && x + w <= clip.x1 && y + h <= clip.y1;
}

constexpr uint16_t kCard = 0x1082;
constexpr uint16_t kCardQuiet = 0x0841;
constexpr uint16_t kCardCancel = 0x4000;

void strokeClip(const Clip& clip, int x, int y, int w, int h, uint16_t color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const int x1 = x + w > clip.x1 ? clip.x1 : x + w;
  const int left = x < clip.x0 ? clip.x0 : x;
  if (y >= clip.y0 && y < clip.y1 && x1 > left) {
    clip.gfx->drawFastHLine(left - clip.ox, y - clip.oy, x1 - left, color);
  }
  const int bottom = y + h - 1;
  if (bottom >= clip.y0 && bottom < clip.y1 && x1 > left) {
    clip.gfx->drawFastHLine(left - clip.ox, bottom - clip.oy, x1 - left, color);
  }
  const int y1 = y + h > clip.y1 ? clip.y1 : y + h;
  const int top = y < clip.y0 ? clip.y0 : y;
  if (x >= clip.x0 && x < clip.x1 && y1 > top) {
    clip.gfx->drawFastVLine(x - clip.ox, top - clip.oy, y1 - top, color);
  }
  const int right = x + w - 1;
  if (right >= clip.x0 && right < clip.x1 && y1 > top) {
    clip.gfx->drawFastVLine(right - clip.ox, top - clip.oy, y1 - top, color);
  }
}

void fillClip(const Clip& clip, int x, int y, int w, int h, uint16_t color) {
  int left = x < clip.x0 ? clip.x0 : x;
  int top = y < clip.y0 ? clip.y0 : y;
  int right = x + w > clip.x1 ? clip.x1 : x + w;
  int bottom = y + h > clip.y1 ? clip.y1 : y + h;
  if (right <= left || bottom <= top) {
    return;
  }
  clip.gfx->fillRect(left - clip.ox, top - clip.oy, right - left, bottom - top, color);
}

void textClip(const Clip& clip, int x, int y, int size, uint16_t color, const char* text) {
  const int h = size * 8;
  if (y < clip.y0 || y + h > clip.y1 || x >= clip.x1) {
    return;
  }
  clip.gfx->setTextSize(size);
  clip.gfx->setTextColor(color);
  clip.gfx->setCursor(x - clip.ox, y - clip.oy);
  clip.gfx->print(text == nullptr ? "" : text);
}

const char* securityLabel(bool secure) { return secure ? "SEC" : "OPEN"; }

bool intersects(const UiControl& control, const Clip& clip) {
  return control.x < clip.x1 && control.x + control.w > clip.x0 && control.y < clip.y1 && control.y + control.h > clip.y0;
}

void paintIcon(const Clip& clip, int id, int x, int y, uint16_t ink) {
  if (!boxInside(clip, x, y, 12, 12)) {
    return;
  }
  const int dx = x - clip.ox;
  const int dy = y - clip.oy;
  switch (id) {
    case IdStart:
    case IdResume:
      clip.gfx->fillTriangle(dx, dy, dx, dy + 10, dx + 8, dy + 5, ink);
      break;
    case IdPause:
      clip.gfx->fillRect(dx, dy, 3, 11, ink);
      clip.gfx->fillRect(dx + 6, dy, 3, 11, ink);
      break;
    case IdStop:
      clip.gfx->fillRect(dx, dy, 10, 10, ink);
      break;
    case IdHosts:
      clip.gfx->fillRect(dx, dy, 11, 2, ink);
      clip.gfx->fillRect(dx, dy + 4, 11, 2, ink);
      clip.gfx->fillRect(dx, dy + 8, 11, 2, ink);
      break;
    case IdBack:
      clip.gfx->drawLine(dx + 7, dy, dx + 1, dy + 5, ink);
      clip.gfx->drawLine(dx + 1, dy + 5, dx + 7, dy + 10, ink);
      break;
    case IdSettings:
      clip.gfx->fillRect(dx, dy + 1, 11, 2, ink);
      clip.gfx->fillRect(dx, dy + 5, 11, 2, ink);
      clip.gfx->fillRect(dx, dy + 9, 11, 2, ink);
      break;
    default:
      break;
  }
}

bool iconFor(int id) {
  return id == IdStart || id == IdResume || id == IdPause || id == IdStop || id == IdHosts || id == IdBack ||
         id == IdSettings;
}

void paintControl(const Clip& clip, const UiControl& control, bool pressed) {
  if (!intersects(control, clip)) {
    return;
  }
  const ControlFace face = controlFace(control.latched, pressed);
  uint16_t fill = kCard;
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
  } else if (control.dim) {
    fill = kCardQuiet;
    ink = DARKGREY;
    border = DARKGREY;
  } else if (control.cancel) {
    fill = kCardCancel;
    border = ORANGE;
  } else if (control.secondary) {
    fill = kCardQuiet;
    border = 0x7BEF;
  }
  if (!control.chrome) {
    fillClip(clip, control.x, control.y, control.w, control.h, fill);
    strokeClip(clip, control.x, control.y, control.w, control.h, border);
  }
  const bool single = control.label[0] != '\0' && control.label[1] == '\0';
  const bool icon = !single && control.w >= 64 && iconFor(control.id);
  if (icon) {
    paintIcon(clip, control.id, control.x + 6, control.y + (control.h / 2) - 5, ink);
  }
  const int size = single ? 2 : 1;
  const int textW = single ? 12 : 6 * static_cast<int>(strlen(control.label));
  int textX = control.x + (icon ? 20 : 6);
  if (single) {
    textX = control.x + (control.w - textW) / 2;
    if (textX < control.x + 1) {
      textX = control.x + 1;
    }
  }
  int textY = control.detail[0] == '\0' ? control.y + (control.h / 2) - (single ? 8 : 4) : control.y + 8;
  int detailY = control.id == IdProgress ? control.y + 16 : control.y + 26;
  int vendorY = control.y + 36;
  if (control.id >= IdRow0 && control.id < IdRow0 + 6) {
    int placed = 0;
    if (uiHostTextY(control.y, control.h, 0, 8, &placed)) {
      textY = placed;
    }
    if (uiHostTextY(control.y, control.h, 1, 8, &placed)) {
      detailY = placed;
    }
    if (uiHostTextY(control.y, control.h, 2, 8, &placed)) {
      vendorY = placed;
    }
  }
  int noteY = 0;
  const bool noteOk = control.id >= IdRow0 && control.id < IdRow0 + 6 &&
                      uiHostTextY(control.y, control.h, 3, 8, &noteY);
  if (control.id == IdProgress) {
    textY = control.y + 4;
  }
  textClip(clip, textX, textY, size, ink, control.label);
  if (control.detail[0] != '\0') {
    uint16_t detailInk = ink;
    if (face == ControlFace::Normal || face == ControlFace::Latched) {
      if (strcmp(control.detail, "OPEN") == 0) {
        detailInk = GREEN;
      } else if (strcmp(control.detail, "CLOSED") == 0) {
        detailInk = DARKGREY;
      } else if (strcmp(control.detail, "TIMEOUT") == 0) {
        detailInk = ORANGE;
      } else if (strcmp(control.detail, "ERROR") == 0) {
        detailInk = RED;
      }
    }
    textClip(clip, control.x + 6, detailY, 1, detailInk, control.detail);
  }
  if (control.vendor[0] != '\0' && control.h >= 44 && control.id != IdProgress) {
    textClip(clip, control.x + 6, vendorY, 1, ink, control.vendor);
  }
  if (noteOk && control.note[0] != '\0') {
    textClip(clip, control.x + 6, noteY, 1, ink, control.note);
  }
}

void paintAddressBar(const Clip& clip, uint16_t processed, uint16_t candidates) {
  const int percent = progressPercent(processed, candidates);
  const int x = 14;
  const int y = 270;
  const int w = 194;
  const int h = 8;
  if (!boxInside(clip, x, y, w, h)) {
    return;
  }
  clip.gfx->drawRect(x - clip.ox, y - clip.oy, w, h, CYAN);
  const int inner = w - 2;
  int fillW = (inner * percent) / 100;
  if (percent > 0 && fillW < 1) {
    fillW = 1;
  }
  if (fillW > 0) {
    clip.gfx->fillRect(x + 1 - clip.ox, y + 1 - clip.oy, fillW, h - 2, GREEN);
  }
}

void ensureSprite() {
  if (gSprite != nullptr || !deviceDisplay().ready() || !psramFound()) {
    return;
  }
  gSprite = new Arduino_Canvas(kUiSpriteW, kUiSpriteH, &deviceDisplay().panel(), 0, 0);
  if (gSprite == nullptr || !gSprite->begin(GFX_SKIP_OUTPUT_BEGIN) || gSprite->getFramebuffer() == nullptr) {
    delete gSprite;
    gSprite = nullptr;
  }
}

}  // namespace

void ScannerUi::begin(WifiService& wifi, ScannerController& scanner) {
  wifi_ = &wifi;
  scanner_ = &scanner;
  drawnValid_ = false;
  bool fromNvs = false;
  profile_ = loadServiceProfile(&fromNvs);
  Serial.printf("WLS profile=%s source=%s\n", serviceProfileToken(profile_), fromNvs ? "nvs" : "default");
  bool countFromNvs = false;
  const uint16_t count = loadAddressRangeCount(&countFromNvs);
  scanner_->setLimit(count);
  Serial.printf("WLS range count=%u source=%s\n", static_cast<unsigned>(count), countFromNvs ? "nvs" : "default");
  ensureSprite();
  Serial.printf("WLS ui sprite pool=%s bytes=%u w=%d h=%d\n", gSprite != nullptr ? "psram" : "none",
                static_cast<unsigned>(kUiSpriteW * kUiSpriteH * 2), kUiSpriteW, kUiSpriteH);
}

void ScannerUi::rebuildHostView() const {
  viewCount_ = 0;
  detailCount_ = 0;
  if (scanner_ == nullptr) {
    return;
  }
  const uint16_t observed = scanner_->observedCount();
  const int hostCount = observed > HostInventory::kCap ? static_cast<int>(HostInventory::kCap) : static_cast<int>(observed);
  for (int i = 0; i < hostCount; ++i) {
    const ObservedHost* host = scanner_->hostAt(static_cast<uint16_t>(i));
    viewIps_[i] = host != nullptr ? host->ip : Ipv4();
  }
  struct Lookup {
    const ServiceScan* scan;
  } lookup{scanner_->serviceScan()};
  auto at = [](void* context, uint16_t index) -> const ServiceHostResult* {
    auto* held = static_cast<Lookup*>(context);
    if (held == nullptr || held->scan == nullptr) {
      return nullptr;
    }
    return held->scan->resultAt(index);
  };
  viewCount_ = buildServiceHostView(viewOrder_, static_cast<int>(HostInventory::kCap), hostCount, viewIps_, at, &lookup,
                                    openOnly_);
  if (detailIndex_ >= 0) {
    const ServiceScan* services = scanner_->serviceScan();
    const ServiceHostResult* result =
        services != nullptr ? services->resultAt(static_cast<uint16_t>(detailIndex_)) : nullptr;
    detailCount_ = result != nullptr ? result->tested : 0;
  }
}

void ScannerUi::logServiceView() const {
  if (scanner_ == nullptr) {
    return;
  }
  rebuildHostView();
  const ServiceScan* services = scanner_->serviceScan();
  const ServiceProfile labelProfile =
      services != nullptr && services->run() != ServiceRun::Idle ? services->profile() : profile_;
  Serial.printf("WLS view filter=%s shown=%d observed=%u openHosts=%u\n", openOnly_ ? "open" : "all", viewCount_,
                static_cast<unsigned>(scanner_->observedCount()),
                static_cast<unsigned>(services != nullptr ? services->openHosts() : 0));
  const int limit = viewCount_ < 32 ? viewCount_ : 32;
  for (int i = 0; i < limit; ++i) {
    const uint16_t index = viewOrder_[i];
    const ObservedHost* host = scanner_->hostAt(index);
    if (host == nullptr) {
      continue;
    }
    char ip[16];
    char summary[22];
    char csv[160];
    formatIpv4(host->ip, ip, sizeof(ip));
    const ServiceHostResult* result = services != nullptr ? services->resultAt(index) : nullptr;
    if (!formatServiceSummary(summary, sizeof(summary), labelProfile, result)) {
      summary[0] = '\0';
    }
    if (!formatServiceField(csv, sizeof(csv), labelProfile, result)) {
      csv[0] = '\0';
    }
    Serial.printf("WLS host ip=%s tested=%u open=%u summary=%s csv=%s\n", ip,
                  static_cast<unsigned>(result != nullptr ? result->tested : 0),
                  static_cast<unsigned>(result != nullptr ? result->openCount : 0), summary, csv);
  }
  reportResource("after-view");
}

void ScannerUi::fillSnapshot(UiSnapshot& snapshot) const {
  snapshot = UiSnapshot();
  const WifiPhase phase = wifi_->phase();
  if (phase == WifiPhase::Results) {
    snapshot.phase = UiPhase::Results;
  } else if (phase == WifiPhase::Password) {
    snapshot.phase = UiPhase::Password;
  } else if (showingSettings_) {
    snapshot.phase = UiPhase::Settings;
  } else if (showingHosts_) {
    snapshot.phase = UiPhase::Hosts;
  } else {
    snapshot.phase = UiPhase::Home;
  }
  snapshot.saved = wifi_->hasSavedNetwork();
  snapshot.shift = wifi_->shiftOn();
  snapshot.keyboardPage = keyboardPage_;
  snapshot.listPage = page_;
  snapshot.scan = scanner_->state();
  const ServiceScan* services = scanner_->serviceScan();
  if (services != nullptr && services->running()) {
    snapshot.scan = ScanState::Scanning;
  } else if (services != nullptr && services->paused()) {
    snapshot.scan = ScanState::Paused;
  }
  snapshot.profile = profile_;
  snapshot.settingsPage = settingsPage_;
  copyLabel(snapshot.editText, sizeof(snapshot.editText), editText_);
  const RangePreview range = scanner_->preview();
  snapshot.rangeAutomatic = range.mode != RangeMode::Custom;
  snapshot.rangeCanPrev = range.canPrev;
  snapshot.rangeCanNext = range.canNext;
  snapshot.rangeLimit = addressLimitOk(range.limit) ? range.limit : 256;
  if (range.valid) {
    formatIpv4(range.start, snapshot.rangeStart, sizeof(snapshot.rangeStart));
    formatIpv4(range.end, snapshot.rangeEnd, sizeof(snapshot.rangeEnd));
    snprintf(snapshot.rangeNote, sizeof(snapshot.rangeNote), range.clamped ? "Clamped %u" : "%u addresses",
             static_cast<unsigned>(range.count));
  } else {
    copyLabel(snapshot.rangeNote, sizeof(snapshot.rangeNote), range.reason[0] != '\0' ? range.reason : "Join Wi-Fi");
  }
  if (snapshot.phase == UiPhase::Results) {
    const int start = page_ * 6;
    for (int row = 0; row < 6; ++row) {
      const WifiAp* ap = wifi_->resultAt(start + row);
      if (ap == nullptr) {
        continue;
      }
      snapshot.rowPresent[row] = true;
      const char* selected = wifi_->selectedSsid();
      snapshot.rowSelected[row] = selected != nullptr && selected[0] != '\0' && strcmp(selected, ap->ssid) == 0;
      copyLabel(snapshot.rowLabel[row], sizeof(snapshot.rowLabel[row]), ap->ssid);
      snprintf(snapshot.rowDetail[row], sizeof(snapshot.rowDetail[row]), "%s %ld dBm", securityLabel(ap->secure),
               static_cast<long>(ap->rssi));
    }
  } else if (snapshot.phase == UiPhase::Hosts) {
    rebuildHostView();
    const ServiceProfile labelProfile =
        services != nullptr && services->run() != ServiceRun::Idle ? services->profile() : profile_;
    snapshot.openOnly = openOnly_;
    snapshot.visibleCount = viewCount_;
    snapshot.hostDetail = false;
    if (detailIndex_ >= 0 && scanner_->hostAt(static_cast<uint16_t>(detailIndex_)) == nullptr) {
      detailIndex_ = -1;
      detailPage_ = 0;
      detailCount_ = 0;
    }
    if (detailIndex_ >= 0) {
      snapshot.hostDetail = true;
      snapshot.detailPage = detailPage_;
      const ObservedHost* host = scanner_->hostAt(static_cast<uint16_t>(detailIndex_));
      if (host != nullptr) {
        formatIpv4(host->ip, snapshot.detailTitle, sizeof(snapshot.detailTitle));
        copyLabel(snapshot.detailName, sizeof(snapshot.detailName), host->name[0] != '\0' ? host->name : "No name");
      }
      const ServiceHostResult* result =
          services != nullptr ? services->resultAt(static_cast<uint16_t>(detailIndex_)) : nullptr;
      int tested = result != nullptr ? result->tested : 0;
      if (tested > kServicePortCap) {
        tested = kServicePortCap;
      }
      detailCount_ = tested;
      if (tested <= 0) {
        copyLabel(snapshot.emptyNote, sizeof(snapshot.emptyNote), "Not scanned");
      } else {
        if (detailPage_ < 0) {
          detailPage_ = 0;
        }
        if (detailPage_ * 6 >= tested) {
          detailPage_ = (tested - 1) / 6;
        }
        snapshot.detailPage = detailPage_;
        const int start = detailPage_ * 6;
        for (int row = 0; row < 6; ++row) {
          const int index = start + row;
          if (result == nullptr || index >= tested || index >= kServicePortCap) {
            continue;
          }
          snapshot.rowPresent[row] = true;
          const uint16_t port = serviceProfilePort(labelProfile, static_cast<uint8_t>(index));
          const char* family = serviceProfilePortFamily(labelProfile, static_cast<uint8_t>(index));
          formatServicePortLabel(snapshot.rowLabel[row], sizeof(snapshot.rowLabel[row]), port, family);
          copyLabel(snapshot.rowDetail[row], sizeof(snapshot.rowDetail[row]),
                    serviceStateWord(result->state[index]));
        }
      }
    } else {
      if (viewCount_ > 0) {
        if (page_ < 0) {
          page_ = 0;
        }
        if (page_ * 6 >= viewCount_) {
          page_ = (viewCount_ - 1) / 6;
        }
      }
      if (viewCount_ == 0) {
        copyLabel(snapshot.emptyNote, sizeof(snapshot.emptyNote),
                  openOnly_ && scanner_->observedCount() > 0 ? "No open ports" : "No hosts yet");
      }
      const int start = page_ * 6;
      for (int row = 0; row < 6; ++row) {
        if (start + row >= viewCount_) {
          continue;
        }
        const uint16_t index = viewOrder_[start + row];
        const ObservedHost* host = scanner_->hostAt(index);
        if (host == nullptr) {
          continue;
        }
        snapshot.rowPresent[row] = true;
        formatIpv4(host->ip, snapshot.rowLabel[row], sizeof(snapshot.rowLabel[row]));
        formatHostDetail(snapshot.rowDetail[row], sizeof(snapshot.rowDetail[row]), host->nameSource, host->name,
                         host->hasMac, host->mac);
        formatOuiLine(snapshot.rowVendor[row], sizeof(snapshot.rowVendor[row]), host->ouiState, host->manufacturer);
        const ServiceHostResult* result = services != nullptr ? services->resultAt(index) : nullptr;
        formatServiceSummary(snapshot.rowNote[row], sizeof(snapshot.rowNote[row]), labelProfile, result);
      }
    }
  } else if (snapshot.phase == UiPhase::Home) {
    snapshot.showDashboard = true;
    formatAddressProgressLabel(snapshot.progressLabel, sizeof(snapshot.progressLabel), scanner_->processedCount(),
                               scanner_->candidateCount());
    const unsigned long seconds = static_cast<unsigned long>(scanner_->elapsedMs() / 1000UL);
    if (scanner_->hasCurrent()) {
      char current[16];
      formatIpv4(scanner_->currentAddress(), current, sizeof(current));
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "%s %lus", current, seconds);
    } else if (scanner_->hasLast()) {
      char last[16];
      formatIpv4(scanner_->lastAddress(), last, sizeof(last));
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "last %s", last);
    } else {
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "elapsed %lus", seconds);
    }
    if (services != nullptr && services->run() != ServiceRun::Idle) {
      formatServiceProgressLabel(snapshot.progressLabel, sizeof(snapshot.progressLabel), services->completed(),
                                 services->planned());
      if (services->running() || services->paused() || services->run() == ServiceRun::Complete ||
          services->run() == ServiceRun::Stopped) {
        formatServiceProgressDetail(snapshot.progressDetail, sizeof(snapshot.progressDetail),
                                    serviceProfileToken(services->profile()), services->openHosts());
      }
    } else if (services != nullptr && scanner_->state() == ScanState::Complete) {
      snprintf(snapshot.progressDetail, sizeof(snapshot.progressDetail), "Naming");
    }
    formatDevicesFoundLabel(snapshot.newestLabel, sizeof(snapshot.newestLabel), scanner_->observedCount());
    const ObservedHost* newest = scanner_->newest();
    if (newest == nullptr) {
      copyLabel(snapshot.newestDetail, sizeof(snapshot.newestDetail), "none yet");
    } else {
      formatIpv4(newest->ip, snapshot.newestDetail, sizeof(snapshot.newestDetail));
      formatOuiLine(snapshot.newestVendor, sizeof(snapshot.newestVendor), newest->ouiState, newest->manufacturer);
    }
  }
}

UiPaintFrame ScannerUi::makeFrame(const UiSnapshot& snapshot, const UiControl* controls, int count) const {
  UiPaintFrame frame;
  switch (snapshot.phase) {
    case UiPhase::Results:
      frame.screen = UiPaintScreen::Results;
      break;
    case UiPhase::Password:
      frame.screen = UiPaintScreen::Password;
      break;
    case UiPhase::Hosts:
      frame.screen = UiPaintScreen::Hosts;
      break;
    case UiPhase::Settings:
      frame.screen = UiPaintScreen::Settings;
      break;
    case UiPhase::Home:
      frame.screen = UiPaintScreen::Home;
      break;
  }
  frame.scan = static_cast<int>(snapshot.scan);
  frame.saved = wifi_->hasSavedNetwork();
  frame.station = wifi_->phase() == WifiPhase::Connected;
  frame.processed = scanner_->processedCount();
  frame.candidates = scanner_->candidateCount();
  frame.observed = scanner_->observedCount();
  frame.elapsedSec = scanner_->elapsedMs() / 1000UL;
  if (snapshot.phase == UiPhase::Settings) {
    frame.page = static_cast<int>(snapshot.settingsPage);
  } else if (snapshot.hostDetail) {
    frame.page = snapshot.detailPage;
  } else {
    frame.page = page_;
  }
  frame.keyboardPage = keyboardPage_;
  frame.shift = wifi_->shiftOn();
  frame.passLen = wifi_->passwordLength();
  frame.store = static_cast<int>(lastInventoryStore().status);
  frame.profile = static_cast<int>(profile_);
  if (scanner_->serviceScan() != nullptr) {
    frame.svcDone = scanner_->serviceScan()->completed();
    frame.svcPlan = scanner_->serviceScan()->planned();
  }
  copyLabel(frame.path, sizeof(frame.path), lastInventoryStore().path);
  copyLabel(frame.status, sizeof(frame.status), wifi_->statusText());
  copyLabel(frame.newest, sizeof(frame.newest), snapshot.newestLabel);
  copyLabel(frame.newestDetail, sizeof(frame.newestDetail), snapshot.newestDetail);
  uint32_t stamp = 2166136261u;
  for (int row = 0; row < 6; ++row) {
    stamp = mixText(stamp, snapshot.rowPresent[row] ? "1" : "0");
    stamp = mixText(stamp, snapshot.rowLabel[row]);
    stamp = mixText(stamp, snapshot.rowDetail[row]);
    stamp = mixText(stamp, snapshot.rowVendor[row]);
    stamp = mixText(stamp, snapshot.rowNote[row]);
  }
  stamp = mixText(stamp, snapshot.openOnly ? "open" : "all");
  stamp = mixText(stamp, snapshot.hostDetail ? "detail" : "list");
  stamp = mixText(stamp, snapshot.detailTitle);
  stamp = mixText(stamp, snapshot.emptyNote);
  if (snapshot.phase == UiPhase::Settings) {
    stamp = mixText(stamp, snapshot.rangeStart);
    stamp = mixText(stamp, snapshot.rangeEnd);
    stamp = mixText(stamp, snapshot.rangeNote);
    stamp = mixText(stamp, snapshot.editText);
  }
  frame.listStamp = stamp;
  const int shown = remoteAck_.pending() ? remoteAck_.shownId() : press_.shownId();
  frame.shownId = shown;
  if (shown >= 0 && controls != nullptr) {
    for (int i = 0; i < count; ++i) {
      if (controls[i].id == shown) {
        frame.shownY = controls[i].y;
        frame.shownH = controls[i].h;
        break;
      }
    }
  } else if (drawnValid_) {
    frame.shownY = drawn_.shownY;
    frame.shownH = drawn_.shownH;
  }
  return frame;
}

void ScannerUi::paintMasked(uint32_t mask, const UiSnapshot& snapshot, const UiControl* controls, int count, int shownId) {
  if (mask == UiRegionInPlace) {
    Clip panel{&deviceDisplay().panel(), 0, 0, 0, 0, kPanelWidth, kPanelHeight};
    for (int i = 0; i < count; ++i) {
      if (controls[i].id == drawn_.shownId || controls[i].id == shownId) {
        paintControl(panel, controls[i], controls[i].id == shownId);
      }
    }
    return;
  }
  static const uint32_t kBits[] = {UiRegionHeader, UiRegionWifiActions, UiRegionNetwork, UiRegionProgress,
                                    UiRegionLatest, UiRegionControls,    UiRegionFooter};
  for (uint32_t bit : kBits) {
    if ((mask & bit) == 0) {
      continue;
    }
    const UiRegionRect rect = uiRegionRect(bit);
    if (rect.h <= 0 || rect.h > kUiSpriteH) {
      continue;
    }
    Arduino_GFX* gfx = &deviceDisplay().panel();
    int oy = 0;
    if (gSprite != nullptr) {
      gSprite->fillScreen(BLACK);
      gfx = gSprite;
      oy = rect.y;
    } else {
      deviceDisplay().panel().fillRect(rect.x, rect.y, rect.w, rect.h, BLACK);
    }
    Clip clip{gfx, 0, oy, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h};
    if (bit == UiRegionHeader) {
      if (snapshot.phase == UiPhase::Hosts) {
        if (snapshot.hostDetail) {
          textClip(clip, 8, 8, 2, CYAN, snapshot.detailTitle[0] != '\0' ? snapshot.detailTitle : "Host");
          textClip(clip, 8, 32, 1, WHITE, snapshot.detailName);
          textClip(clip, 8, 48, 1, DARKGREY, "OPEN CLOSED TIMEOUT ERROR");
        } else {
          textClip(clip, 8, 8, 2, CYAN, "Hosts");
          char line[40];
          if (snapshot.openOnly) {
            snprintf(line, sizeof(line), "Open only %d/%u", snapshot.visibleCount,
                     static_cast<unsigned>(scanner_->observedCount()));
          } else {
            snprintf(line, sizeof(line), "All hosts %u", static_cast<unsigned>(scanner_->observedCount()));
          }
          textClip(clip, 8, 32, 1, WHITE, line);
        }
      } else if (snapshot.phase == UiPhase::Results) {
        textClip(clip, 8, 8, 2, CYAN, "Networks");
      } else if (snapshot.phase == UiPhase::Password) {
        textClip(clip, 8, 8, 2, CYAN, "Password");
        char ssidLine[40];
        snprintf(ssidLine, sizeof(ssidLine), "%.20s", wifi_->selectedSsid());
        textClip(clip, 8, 28, 1, WHITE, ssidLine);
        char mask[80];
        maskPassword(mask, sizeof(mask), wifi_->passwordLength());
        textClip(clip, 8, 40, 1, WHITE, wifi_->passwordLength() == 0 ? "(empty)" : mask);
      } else if (snapshot.phase == UiPhase::Settings) {
        if (snapshot.settingsPage == SettingsPage::Range) {
          textClip(clip, 8, 8, 2, CYAN, "Range");
          textClip(clip, 8, 32, 1, WHITE, snapshot.rangeStart[0] != '\0' ? snapshot.rangeStart : snapshot.rangeNote);
          if (snapshot.rangeEnd[0] != '\0') {
            char endLine[28];
            snprintf(endLine, sizeof(endLine), "to %s", snapshot.rangeEnd);
            textClip(clip, 8, 48, 1, WHITE, endLine);
          }
        } else if (snapshot.settingsPage == SettingsPage::Edit) {
          textClip(clip, 8, 8, 2, CYAN, "Custom start");
          textClip(clip, 8, 36, 1, WHITE, snapshot.editText[0] != '\0' ? snapshot.editText : "Type an address");
        } else if (snapshot.settingsPage == SettingsPage::Service) {
          textClip(clip, 8, 8, 2, CYAN, "Settings");
          textClip(clip, 8, 32, 1, WHITE, "Service scan");
          textClip(clip, 8, 48, 1, DARKGREY, "Saved on this scanner");
        } else {
          textClip(clip, 8, 8, 2, CYAN, "Settings");
          textClip(clip, 8, 32, 1, WHITE, "Service and range");
        }
      } else {
        // Screen names stay short. The product title fits this home header:
        // size 2 is 12 px per character, 16 characters, origin x=8, panel 222.
        textClip(clip, 8, 8, 2, CYAN, "WiFi-LAN-Scanner");
        textClip(clip, 8, 36, 1, WHITE, wifi_->statusText());
        if (wifi_->hasSavedNetwork()) {
          char line[40];
          snprintf(line, sizeof(line), "Saved %.18s", wifi_->savedSsid());
          textClip(clip, 8, 52, 1, WHITE, line);
        }
      }
    } else if (bit == UiRegionNetwork && snapshot.phase == UiPhase::Settings &&
               snapshot.settingsPage == SettingsPage::Range) {
      textClip(clip, 8, 160, 1, WHITE, snapshot.rangeStart);
      textClip(clip, 8, 176, 1, WHITE, snapshot.rangeEnd);
      textClip(clip, 8, 192, 1, GREEN, snapshot.rangeNote);
    } else if (bit == UiRegionNetwork && snapshot.phase == UiPhase::Hosts && snapshot.emptyNote[0] != '\0') {
      textClip(clip, 8, 160, 1, WHITE, snapshot.emptyNote);
    } else if (bit == UiRegionNetwork && snapshot.phase == UiPhase::Home) {
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
        textClip(clip, 8, 148, 1, WHITE, line);
        snprintf(line, sizeof(line), "IP %s", ip);
        textClip(clip, 8, 160, 1, WHITE, line);
        snprintf(line, sizeof(line), "Mask %s", mask);
        textClip(clip, 8, 174, 1, WHITE, line);
        snprintf(line, sizeof(line), "GW %s", gateway);
        textClip(clip, 8, 188, 1, WHITE, line);
        snprintf(line, sizeof(line), "DNS %s", dns);
        textClip(clip, 8, 202, 1, WHITE, line);
        if (range.valid) {
          snprintf(line, sizeof(line), "Net %s/%u", network, range.prefix);
          textClip(clip, 8, 216, 1, GREEN, line);
          snprintf(line, sizeof(line), "Usable %lu cap %lu", static_cast<unsigned long>(range.usableHosts),
                   static_cast<unsigned long>(range.futureScanCap));
          textClip(clip, 8, 228, 1, GREEN, line);
        } else {
          textClip(clip, 8, 216, 1, ORANGE, "Range unavailable");
        }
      } else {
        char banner[64];
        if (formatScanBanner(banner, sizeof(banner), scanner_->state(), false, scanner_->processedCount(),
                             scanner_->candidateCount(), scanner_->observedCount())) {
          textClip(clip, 8, 160, 1, DARKGREY, banner);
        }
      }
    } else if (bit == UiRegionFooter && (snapshot.phase == UiPhase::Home || snapshot.phase == UiPhase::Hosts)) {
      char panel[40];
      if (formatPersistPanel(panel, sizeof(panel), lastInventoryStore())) {
        const InventoryStoreStatus status = lastInventoryStore().status;
        const uint16_t ink = status == InventoryStoreStatus::Stored    ? GREEN
                             : status == InventoryStoreStatus::Failed   ? RED
                             : status == InventoryStoreStatus::Absent   ? ORANGE
                                                                        : DARKGREY;
        textClip(clip, 8, 462, 1, ink, panel);
      }
      if (snapshot.phase == UiPhase::Home && !deviceTouch().ready()) {
        textClip(clip, 8, 470, 1, RED, "Touch controller absent");
      }
    }
    for (int i = 0; i < count; ++i) {
      paintControl(clip, controls[i], controls[i].id == shownId);
    }
    if (bit == UiRegionProgress && snapshot.phase == UiPhase::Home) {
      paintAddressBar(clip, scanner_->processedCount(), scanner_->candidateCount());
    }
    if (gSprite != nullptr) {
      deviceDisplay().panel().draw16bitRGBBitmap(rect.x, rect.y, gSprite->getFramebuffer(), rect.w, rect.h);
    }
  }
}

void ScannerUi::servicePaint() {
  if (!deviceDisplay().ready() || wifi_ == nullptr || scanner_ == nullptr) {
    return;
  }
  UiSnapshot snapshot;
  fillSnapshot(snapshot);
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, snapshot);
  const UiPaintFrame frame = makeFrame(snapshot, controls, count);
  const uint32_t mask = drawnValid_ ? dirtyRegions(drawn_, frame) : UiRegionAll;
  if (mask == 0) {
    return;
  }
  const int shown = remoteAck_.pending() ? remoteAck_.shownId() : press_.shownId();
  paintMasked(mask, snapshot, controls, count, shown);
  const bool full = (mask & UiRegionAll) == UiRegionAll;
  const uint32_t now = millis();
  const bool progressOnly = mask == UiRegionProgress || mask == (UiRegionProgress | UiRegionNetwork);
  const bool suppress = (!full && progressOnly && lastLoggedMask_ == mask && now - lastPaintLogMs_ < 1000) ||
                        (!full && !progressOnly && lastLoggedMask_ == mask && now - lastPaintLogMs_ < 500);
  if (!suppress) {
    Serial.printf("WLS ui paint full=%d mask=%lu\n", full ? 1 : 0, static_cast<unsigned long>(mask));
    lastPaintLogMs_ = now;
    lastLoggedMask_ = mask;
  }
  drawn_ = frame;
  drawnValid_ = true;
}

void ScannerUi::paintAckNow(int controlId) {
  if (!deviceDisplay().ready() || wifi_ == nullptr || scanner_ == nullptr) {
    return;
  }
  UiSnapshot snapshot;
  fillSnapshot(snapshot);
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, snapshot);
  const int shown = remoteAck_.pending() ? remoteAck_.shownId() : press_.shownId();
  Clip panel{&deviceDisplay().panel(), 0, 0, 0, 0, kPanelWidth, kPanelHeight};
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == controlId) {
      paintControl(panel, controls[i], controls[i].id == shown);
      if (drawnValid_) {
        drawn_.shownId = shown;
        drawn_.shownY = controls[i].y;
        drawn_.shownH = controls[i].h;
      }
      break;
    }
  }
}

void ScannerUi::noteProfile(ServiceProfile next) {
  if (next == profile_) {
    return;
  }
  profile_ = next;
  saveServiceProfile(profile_);
  Serial.printf("WLS profile=%s source=set\n", serviceProfileToken(profile_));
}

int ScannerUi::hitControl(int x, int y) const {
  UiSnapshot snapshot;
  fillSnapshot(snapshot);
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, snapshot);
  return hitUiControl(controls, count, x, y);
}

void ScannerUi::dispatch(int id) {
  if (showingSettings_ && settingsPage_ == SettingsPage::Edit) {
    static const char kDigits[] = "123456789.0";
    if (id >= IdKeyBase && id < IdKeyBase + 11) {
      const size_t n = strlen(editText_);
      if (n + 1 < sizeof(editText_)) {
        editText_[n] = kDigits[id - IdKeyBase];
        editText_[n + 1] = '\0';
      }
      return;
    }
    if (id == IdDel) {
      const size_t n = strlen(editText_);
      if (n > 0) {
        editText_[n - 1] = '\0';
      }
      return;
    }
    if (id == IdOk) {
      applyRemote(AppAction::SetCustom, -1, editText_);
      return;
    }
  }
  if (id >= IdKeyBase && id < IdRow0) {
    UiSnapshot snapshot;
    fillSnapshot(snapshot);
    int cap = 0;
    UiControl* controls = uiScratchControls(&cap);
    const int count = collectUiControls(controls, cap, snapshot);
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

bool ScannerUi::executeRemote(AppAction action, int rowOffset, const char* text) {
  if (wifi_ == nullptr || scanner_ == nullptr || action == AppAction::None) {
    return false;
  }
  const uint16_t limitBefore = scanner_->preview().limit;
  const SettingsPage pageBefore = settingsPage_;
  AppView view;
  view.showingHosts = showingHosts_;
  view.showingSettings = showingSettings_;
  view.entryOpen = wifi_->phase() == WifiPhase::Password;
  view.resultsOpen = wifi_->phase() == WifiPhase::Results;
  if (showingHosts_) {
    rebuildHostView();
  }
  view.page = page_;
  view.keyboardPage = keyboardPage_;
  view.resultCount = wifi_->resultCount();
  view.observedCount = scanner_->observedCount();
  view.rowOffset = rowOffset;
  view.profile = profile_;
  view.settingsPage = settingsPage_;
  view.openOnly = openOnly_;
  view.detailIndex = detailIndex_;
  view.detailPage = detailPage_;
  view.visibleCount = viewCount_;
  view.detailCount = detailCount_;
  view.selectedInventory = -1;
  if (action == AppAction::SelectRow && showingHosts_ && detailIndex_ < 0 && rowOffset >= 0 && rowOffset < 6) {
    const int slot = page_ * 6 + rowOffset;
    if (slot >= 0 && slot < viewCount_) {
      view.selectedInventory = static_cast<int>(viewOrder_[slot]);
    }
  }
  const bool wasHosts = showingHosts_;
  const bool wasOpen = openOnly_;
  AppHooks hooks;
  hooks.findNetworks = hookFind;
  hooks.forgetNetwork = hookForget;
  hooks.selectResult = hookSelect;
  hooks.toggleShift = hookShift;
  hooks.backspace = hookBackspace;
  hooks.submitPassword = hookSubmit;
  hooks.cancelPassword = hookCancel;
  hooks.closeResults = hookClose;
  hooks.context = wifi_;
  const bool ok = applyAppAction(action, view, *scanner_, &hooks, text);
  showingHosts_ = view.showingHosts;
  showingSettings_ = view.showingSettings;
  page_ = view.page;
  keyboardPage_ = view.keyboardPage;
  openOnly_ = view.openOnly;
  detailIndex_ = view.detailIndex;
  detailPage_ = view.detailPage;
  if (view.settingsPage == SettingsPage::Edit && pageBefore != SettingsPage::Edit) {
    editText_[0] = '\0';
  }
  settingsPage_ = view.settingsPage;
  if (view.profile != profile_) {
    noteProfile(view.profile);
  }
  const uint16_t limitAfter = scanner_->preview().limit;
  if (ok && limitAfter != limitBefore && addressLimitOk(limitAfter)) {
    saveAddressRangeCount(limitAfter);
    Serial.printf("WLS range count=%u source=set\n", static_cast<unsigned>(limitAfter));
  }
  if (ok && showingHosts_ && (!wasHosts || openOnly_ != wasOpen || action == AppAction::ResetScan)) {
    logServiceView();
  }
  return ok;
}

void ScannerUi::serviceRemoteAck(uint32_t nowMs) {
  if (!remoteAck_.pending()) {
    return;
  }
  const int shown = remoteAck_.shownId();
  if (!remoteAck_.consume(nowMs)) {
    return;
  }
  const AppAction action = pendingAction_;
  pendingAction_ = AppAction::None;
  const int row = pendingRow_;
  pendingRow_ = -1;
  char text[16];
  text[0] = '\0';
  const bool hasText = pendingTextSet_;
  if (hasText) {
    memcpy(text, pendingText_, sizeof(text));
  }
  pendingTextSet_ = false;
  pendingText_[0] = '\0';
  Serial.printf("WLS ui ack fire control=%s\n", uiControlName(shown));
  Serial.flush();
  executeRemote(action, row, hasText ? text : nullptr);
}

bool ScannerUi::applyRemote(AppAction action, int rowOffset, const char* text) {
  remoteBusy_ = false;
  if (wifi_ == nullptr || scanner_ == nullptr || action == AppAction::None) {
    return false;
  }
  if (action == AppAction::SelectRow && (rowOffset < 0 || rowOffset > 5)) {
    return false;
  }
  if (action == AppAction::SetCustom && text != nullptr) {
    Ipv4 start;
    if (text[0] == '\0' || !parseIpv4(text, start) || !scanner_->acceptsCustomStart(start)) {
      return false;
    }
  }
  if (action == AppAction::SetLimit && !addressLimitOk(static_cast<uint16_t>(rowOffset))) {
    return false;
  }
  UiSnapshot snapshot;
  fillSnapshot(snapshot);
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, snapshot);
  const int id = visibleControlForAction(action, rowOffset, controls, count);
  if (id >= 0) {
    bool latched = false;
    for (int i = 0; i < count; ++i) {
      if (controls[i].id == id) {
        latched = controls[i].latched;
        break;
      }
    }
    pendingAction_ = action;
    pendingRow_ = rowOffset;
    pendingTextSet_ = false;
    pendingText_[0] = '\0';
    if (text != nullptr) {
      size_t n = 0;
      for (; text[n] != '\0' && n + 1 < sizeof(pendingText_); ++n) {
        pendingText_[n] = text[n];
      }
      pendingText_[n] = '\0';
      pendingTextSet_ = true;
    }
    if (!remoteAck_.arm(id, millis())) {
      pendingAction_ = AppAction::None;
      pendingRow_ = -1;
      pendingTextSet_ = false;
      pendingText_[0] = '\0';
      remoteBusy_ = true;
      return false;
    }
    const ControlFace face = controlFace(latched, true);
    Serial.printf("WLS ui ack arm control=%s face=%s ms=%lu\n", uiControlName(id), faceToken(face),
                  static_cast<unsigned long>(ActionAck::kAckMs));
    paintAckNow(id);
    return true;
  }
  const char* token = actionToken(action);
  if (action == AppAction::SetProfile && rowOffset >= 0 && rowOffset <= 2) {
    token = serviceProfileToken(static_cast<ServiceProfile>(rowOffset));
  } else if (action == AppAction::SetLimit) {
    if (rowOffset == 64) {
      token = "count64";
    } else if (rowOffset == 128) {
      token = "count128";
    } else if (rowOffset == 256) {
      token = "count256";
    }
  }
  Serial.printf("WLS ui ack skip action=%s\n", token);
  return executeRemote(action, rowOffset, text);
}

void ScannerUi::captureState(AppState& out) const {
  if (wifi_ == nullptr || scanner_ == nullptr) {
    out = AppState();
    return;
  }
  AppView view;
  view.showingHosts = showingHosts_;
  view.showingSettings = showingSettings_;
  view.entryOpen = wifi_->phase() == WifiPhase::Password;
  view.resultsOpen = wifi_->phase() == WifiPhase::Results;
  view.page = page_;
  view.keyboardPage = keyboardPage_;
  view.resultCount = wifi_->resultCount();
  view.observedCount = scanner_->observedCount();
  view.profile = profile_;
  view.settingsPage = settingsPage_;
  view.openOnly = openOnly_;
  view.detailIndex = detailIndex_;
  view.detailPage = detailPage_;
  AppWifiView wifi;
  wifi.phase = phaseToken(wifi_->phase());
  const char* ssid = wifi_->hasSavedNetwork() ? wifi_->savedSsid() : wifi_->selectedSsid();
  wifi.ssid = ssid == nullptr ? "" : ssid;
  wifi.saved = wifi_->hasSavedNetwork();
  wifi.shift = wifi_->shiftOn();
  wifi.entry = wifi_->phase() == WifiPhase::Password;
  wifi.results = wifi_->phase() == WifiPhase::Results;
  fillAppState(out, view, *scanner_, wifi, profile_);
  out.ack[0] = '\0';
  if (remoteAck_.pending()) {
    snprintf(out.ack, sizeof(out.ack), "%s", uiControlName(remoteAck_.shownId()));
  }
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
  serviceRemoteAck(millis());
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
  }
  servicePaint();
  // A full settings paint can outlast the 120 ms press. Release the remote
  // ack in this same turn once the highlight time has elapsed.
  serviceRemoteAck(millis());
}
