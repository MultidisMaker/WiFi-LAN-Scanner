#pragma once

#include "ActionAck.h"
#include "AppActions.h"
#include "InventoryStore.h"
#include "ScannerController.h"
#include "UiPress.h"
#include "UiRender.h"
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
  bool showingSettings_ = false;
  ServiceProfile profile_ = ServiceProfile::Common;
  bool drawnValid_ = false;
  UiPaintFrame drawn_{};
  uint32_t lastPaintLogMs_ = 0;
  uint32_t lastLoggedMask_ = 0xffffffffu;

  void fillSnapshot(UiSnapshot& snapshot) const;
  UiPaintFrame makeFrame(const UiSnapshot& snapshot, const UiControl* controls, int count) const;
  void paintMasked(uint32_t mask, const UiSnapshot& snapshot, const UiControl* controls, int count, int shownId);
  void servicePaint();
  void paintAckNow(int controlId);
  void noteProfile(ServiceProfile next);
  int hitControl(int x, int y) const;
  void dispatch(int id);
  void noteTouch(const char* event, int id, int x, int y, bool includePoint);
  bool executeRemote(AppAction action, int rowOffset);
  void serviceRemoteAck(uint32_t nowMs);
};
