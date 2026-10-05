#include "DisplayBoard.h"

#include "BoardConfig.h"

namespace {
Arduino_DataBus* gBus = nullptr;
Arduino_GFX* gPanel = nullptr;
DisplayBoard* gBoard = nullptr;
}

bool DisplayBoard::begin() {
  gBoard = this;
  pinMode(kSdCs, OUTPUT);
  digitalWrite(kSdCs, HIGH);
  pinMode(kTftCs, OUTPUT);
  digitalWrite(kTftCs, HIGH);
  if (gPanel == nullptr) {
    gBus = new Arduino_ESP32SPI(kTftDc, kTftCs, kTftSck, kTftMosi, kTftMiso);
    gPanel = new Arduino_ST7796(gBus, kTftRst, 0, true, kPanelWidth, kPanelHeight, kPanelColOffset,
                                kPanelRowOffset, kPanelColOffset, kPanelRowOffset);
  }
  ledcSetup(0, 5000, 8);
  ledcAttachPin(kTftBl, 0);
  ledcWrite(0, 255);
  ready_ = gPanel->begin();
  width_ = gPanel->width();
  height_ = gPanel->height();
  if (ready_) {
    gPanel->fillScreen(BLACK);
  }
  return ready_;
}

int DisplayBoard::width() const { return width_; }

int DisplayBoard::height() const { return height_; }

bool DisplayBoard::ready() const { return ready_; }

Arduino_GFX& DisplayBoard::panel() { return *gPanel; }
