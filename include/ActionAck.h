#pragma once

#include <stdint.h>

#include "AppActions.h"
#include "UiModel.h"
#include "UiPress.h"

// One pending Remote press. The same 120 ms constant the touch tracker uses.
// A second arm while one is pending is rejected. There is no command queue.
class ActionAck {
 public:
  static constexpr uint32_t kAckMs = PressTracker::kAckMs;

  bool pending() const { return pending_; }
  int shownId() const { return shownId_; }
  bool arm(int controlId, uint32_t nowMs);
  // True once, when the pressed face has been due for kAckMs. Clears pending.
  bool consume(uint32_t nowMs);

 private:
  bool pending_ = false;
  int shownId_ = -1;
  uint32_t untilMs_ = 0;
};

// Control id when that control is in the current list, otherwise -1.
// A missing control is not invented.
int visibleControlForAction(AppAction action, int rowOffset, const UiControl* controls, int count);

const char* actionToken(AppAction action);
const char* faceToken(ControlFace face);
