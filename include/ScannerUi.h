#pragma once

#include "ActionAck.h"
#include "AppActions.h"
#include "InventoryStore.h"
#include "NetMath.h"
#include "ScannerController.h"
#include "UiPress.h"
#include "UiRender.h"
#include "WifiService.h"

class ScannerUi {
 public:
  void begin(WifiService& wifi, ScannerController& scanner);
  void loop();
  void captureState(AppState& out) const;
  ServiceProfile profile() const { return profile_; }
  // Touch dispatch and USB Remote both end here. Remote cannot inject keystrokes.
  // text carries a custom start address. Null opens the on-device editor.
  bool applyRemote(AppAction action, int rowOffset, const char* text = nullptr);
  bool remoteApplyWasBusy() const { return remoteBusy_; }

 private:
  WifiService* wifi_ = nullptr;
  ScannerController* scanner_ = nullptr;
  PressTracker press_;
  ActionAck remoteAck_;
  AppAction pendingAction_ = AppAction::None;
  int pendingRow_ = -1;
  char pendingText_[16] = {};
  bool pendingTextSet_ = false;
  bool remoteBusy_ = false;
  mutable int page_ = 0;
  int keyboardPage_ = 0;
  bool showingHosts_ = false;
  bool showingSettings_ = false;
  bool openOnly_ = false;
  mutable int detailIndex_ = -1;
  mutable int detailPage_ = 0;
  mutable uint16_t viewOrder_[HostInventory::kCap] = {};
  mutable Ipv4 viewIps_[HostInventory::kCap] = {};
  mutable int viewCount_ = 0;
  mutable int detailCount_ = 0;
  SettingsPage settingsPage_ = SettingsPage::Menu;
  char editText_[16] = {};
  ServiceProfile profile_ = ServiceProfile::Common;
  bool drawnValid_ = false;
  UiPaintFrame drawn_{};
  uint32_t lastPaintLogMs_ = 0;
  uint32_t lastLoggedMask_ = 0xffffffffu;

  void fillSnapshot(UiSnapshot& snapshot) const;
  void rebuildHostView() const;
  void logServiceView() const;
  UiPaintFrame makeFrame(const UiSnapshot& snapshot, const UiControl* controls, int count) const;
  void paintMasked(uint32_t mask, const UiSnapshot& snapshot, const UiControl* controls, int count, int shownId);
  void servicePaint();
  void paintAckNow(int controlId);
  void noteProfile(ServiceProfile next);
  int hitControl(int x, int y) const;
  void dispatch(int id);
  void noteTouch(const char* event, int id, int x, int y, bool includePoint);
  bool executeRemote(AppAction action, int rowOffset, const char* text);
  void serviceRemoteAck(uint32_t nowMs);
};
