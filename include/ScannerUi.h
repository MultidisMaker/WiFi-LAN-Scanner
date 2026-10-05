#pragma once

#include "ActionAck.h"
#include "AppActions.h"
#include "InventoryStore.h"
#include "ScannerController.h"
#include "UiPress.h"
#include "WifiService.h"

class ScannerUi {
 public:
  void begin(WifiService& wifi, ScannerController& scanner);
  void loop();
  void captureState(AppState& out) const;
  // Touch dispatch and USB Remote both end here. Remote cannot inject keystrokes.
  bool applyRemote(AppAction action, int rowOffset);
  bool remoteApplyWasBusy() const { return remoteBusy_; }

 private:
  WifiService* wifi_ = nullptr;
  ScannerController* scanner_ = nullptr;
  PressTracker press_;
  ActionAck remoteAck_;
  AppAction pendingAction_ = AppAction::None;
  int pendingRow_ = -1;
  bool remoteBusy_ = false;
  int page_ = 0;
  int keyboardPage_ = 0;
  bool showingHosts_ = false;
  unsigned long lastDrawMs_ = 0;
  WifiPhase drawnPhase_ = WifiPhase::Idle;
  ScanState drawnScan_ = ScanState::Idle;
  bool drawnShift_ = false;
  int drawnPassLen_ = -1;
  bool drawnSaved_ = false;
  int drawnPage_ = -1;
  int drawnKeyboard_ = -1;
  int drawnObserved_ = -1;
  int drawnProcessed_ = -1;
  bool drawnHosts_ = false;
  InventoryStoreStatus drawnStore_ = InventoryStoreStatus::Unavailable;
  char drawnPath_[64] = {};
  bool force_ = true;

  void draw(bool full);
  void drawChrome();
  void paintControls();
  int hitControl(int x, int y) const;
  void dispatch(int id);
  void noteTouch(const char* event, int id, int x, int y, bool includePoint);
  bool executeRemote(AppAction action, int rowOffset);
  void serviceRemoteAck(uint32_t nowMs);
};
