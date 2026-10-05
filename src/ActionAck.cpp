#include "ActionAck.h"

bool ActionAck::arm(int controlId, uint32_t nowMs) {
  if (pending_ || controlId < 0) {
    return false;
  }
  pending_ = true;
  shownId_ = controlId;
  untilMs_ = nowMs + kAckMs;
  return true;
}

bool ActionAck::consume(uint32_t nowMs) {
  if (!pending_) {
    return false;
  }
  if (static_cast<int32_t>(nowMs - untilMs_) < 0) {
    return false;
  }
  pending_ = false;
  return true;
}

namespace {

int expectedControl(AppAction action, int rowOffset) {
  switch (action) {
    case AppAction::FindNetworks:
      return IdFind;
    case AppAction::ForgetNetwork:
      return IdForget;
    case AppAction::StartScan:
      return IdStart;
    case AppAction::PauseScan:
      return IdPause;
    case AppAction::ResumeScan:
      return IdResume;
    case AppAction::StopScan:
      return IdStop;
    case AppAction::ResetScan:
      return IdReset;
    case AppAction::OpenHosts:
      return IdHosts;
    case AppAction::Back:
      return IdBack;
    case AppAction::NextPage:
      return IdNext;
    case AppAction::PrevPage:
      return IdPrev;
    case AppAction::Shift:
      return IdShift;
    case AppAction::KeyboardPage:
      return IdPage;
    case AppAction::Backspace:
      return IdDel;
    case AppAction::SubmitPassword:
      return IdOk;
    case AppAction::CancelPassword:
      return IdClose;
    case AppAction::SelectRow:
      if (rowOffset < 0 || rowOffset > 5) {
        return -1;
      }
      return IdRow0 + rowOffset;
    case AppAction::None:
      return -1;
  }
  return -1;
}

}  // namespace

int visibleControlForAction(AppAction action, int rowOffset, const UiControl* controls, int count) {
  const int id = expectedControl(action, rowOffset);
  if (id < 0 || controls == nullptr || count <= 0) {
    return -1;
  }
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == id) {
      return id;
    }
  }
  return -1;
}

const char* actionToken(AppAction action) {
  switch (action) {
    case AppAction::FindNetworks:
      return "find";
    case AppAction::ForgetNetwork:
      return "forget";
    case AppAction::StartScan:
      return "start";
    case AppAction::PauseScan:
      return "pause";
    case AppAction::ResumeScan:
      return "resume";
    case AppAction::StopScan:
      return "stop";
    case AppAction::ResetScan:
      return "reset";
    case AppAction::OpenHosts:
      return "hosts";
    case AppAction::Back:
      return "back";
    case AppAction::NextPage:
      return "next";
    case AppAction::PrevPage:
      return "prev";
    case AppAction::Shift:
      return "shift";
    case AppAction::KeyboardPage:
      return "page";
    case AppAction::Backspace:
      return "del";
    case AppAction::SubmitPassword:
      return "ok";
    case AppAction::CancelPassword:
      return "close";
    case AppAction::SelectRow:
      return "row";
    case AppAction::None:
      return "none";
  }
  return "none";
}

const char* faceToken(ControlFace face) {
  switch (face) {
    case ControlFace::Pressed:
      return "pressed";
    case ControlFace::Latched:
      return "latched";
    case ControlFace::LatchedPressed:
      return "latchedpressed";
    case ControlFace::Normal:
      return "normal";
  }
  return "normal";
}
