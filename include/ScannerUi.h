#pragma once

#include "ScannerController.h"
#include "WifiService.h"

class ScannerUi {
 public:
  void begin(WifiService& wifi, ScannerController& scanner);
  void loop();

 private:
  WifiService* wifi_ = nullptr;
  ScannerController* scanner_ = nullptr;
  int page_ = 0;
  int keyboardPage_ = 0;
  unsigned long lastDrawMs_ = 0;
  WifiPhase drawnPhase_ = WifiPhase::Idle;
  ScanState drawnScan_ = ScanState::Idle;
  bool drawnShift_ = false;
  int drawnPassLen_ = -1;
  bool force_ = true;

  void draw(bool full);
  void drawHome();
  void drawResults();
  void drawPassword();
  void handlePress(int x, int y);
  bool hit(int x, int y, int bx, int by, int bw, int bh) const;
  void button(int x, int y, int w, int h, const char* label) const;
};
