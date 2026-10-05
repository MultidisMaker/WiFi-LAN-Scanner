#include "UiPress.h"

ControlFace controlFace(bool latched, bool pressed) {
  if (pressed && latched) {
    return ControlFace::LatchedPressed;
  }
  if (pressed) {
    return ControlFace::Pressed;
  }
  if (latched) {
    return ControlFace::Latched;
  }
  return ControlFace::Normal;
}

char keyGlyph(char base, bool shift) {
  if (shift && base >= 'a' && base <= 'z') {
    return static_cast<char>(base - 'a' + 'A');
  }
  return base;
}

void maskPassword(char* dest, size_t destLen, int length) {
  if (dest == nullptr || destLen == 0) {
    return;
  }
  if (length < 0) {
    length = 0;
  }
  if (static_cast<size_t>(length) >= destLen) {
    length = static_cast<int>(destLen - 1);
  }
  for (int i = 0; i < length; ++i) {
    dest[i] = '*';
  }
  dest[length] = '\0';
}

PressStep PressTracker::update(bool down, int hitId, uint32_t nowMs) {
  PressStep step{};
  step.beganId = -1;
  step.fireId = -1;
  step.cancelId = -1;
  step.shownId = -1;
  if (!down) {
    hitId = -1;
  }

  if (down && !wasDown_) {
    ackId_ = -1;
    ackUntilMs_ = 0;
    if (hitId >= 0) {
      activeId_ = hitId;
      inside_ = true;
      downMs_ = nowMs;
      step.began = true;
      step.beganId = hitId;
    } else {
      activeId_ = -1;
      inside_ = false;
    }
  } else if (down && wasDown_) {
    if (activeId_ >= 0 && hitId != activeId_) {
      step.cancelled = true;
      step.cancelId = activeId_;
      activeId_ = -1;
      inside_ = false;
    }
  } else if (!down && wasDown_) {
    if (activeId_ >= 0 && inside_) {
      step.fire = true;
      step.fireId = activeId_;
      if (static_cast<uint32_t>(nowMs - downMs_) < kAckMs) {
        ackId_ = activeId_;
        ackUntilMs_ = downMs_ + kAckMs;
      } else {
        ackId_ = -1;
        ackUntilMs_ = 0;
      }
    } else if (activeId_ >= 0) {
      step.cancelled = true;
      step.cancelId = activeId_;
    }
    activeId_ = -1;
    inside_ = false;
  }
  wasDown_ = down;

  int shown = -1;
  if (activeId_ >= 0) {
    shown = activeId_;
  } else if (ackId_ >= 0 && static_cast<int32_t>(nowMs - ackUntilMs_) < 0) {
    shown = ackId_;
  } else {
    ackId_ = -1;
  }
  step.visualChanged = shown != shownId_;
  shownId_ = shown;
  step.shownId = shown;
  return step;
}

bool pressTrackerSelfTest() {
  bool ok = true;

  PressTracker tap;
  const PressStep down = tap.update(true, 7, 1000);
  ok = ok && down.began && down.beganId == 7 && !down.fire && tap.shownId() == 7;
  const PressStep still = tap.update(true, 7, 1040);
  ok = ok && !still.fire && !still.began && tap.shownId() == 7;
  const PressStep up = tap.update(false, 7, 1050);
  ok = ok && up.fire && up.fireId == 7 && !up.cancelled && tap.shownId() == 7;
  const PressStep hold = tap.update(false, -1, 1119);
  ok = ok && !hold.fire && tap.shownId() == 7;
  const PressStep done = tap.update(false, -1, 1120);
  ok = ok && !done.fire && tap.shownId() == -1;

  PressTracker drag;
  drag.update(true, 3, 0);
  const PressStep left = drag.update(true, -1, 30);
  ok = ok && left.cancelled && left.cancelId == 3 && !left.fire && drag.shownId() == -1;
  const PressStep dragUp = drag.update(false, 3, 40);
  ok = ok && !dragUp.fire && drag.shownId() == -1;

  PressTracker slide;
  slide.update(true, 4, 0);
  slide.update(true, -1, 10);
  slide.update(true, 4, 20);
  const PressStep slideUp = slide.update(false, 4, 30);
  ok = ok && !slideUp.fire;

  PressTracker repeat;
  repeat.update(true, 1, 0);
  const PressStep first = repeat.update(false, 1, 200);
  ok = ok && first.fire && first.fireId == 1 && repeat.shownId() == -1;
  repeat.update(true, 1, 250);
  const PressStep second = repeat.update(false, 1, 400);
  ok = ok && second.fire && second.fireId == 1;

  PressTracker miss;
  const PressStep missDown = miss.update(true, -1, 0);
  const PressStep missUp = miss.update(false, -1, 20);
  ok = ok && !missDown.began && !missDown.fire && !missUp.fire;

  ok = ok && controlFace(false, false) == ControlFace::Normal;
  ok = ok && controlFace(false, true) == ControlFace::Pressed;
  ok = ok && controlFace(true, false) == ControlFace::Latched;
  ok = ok && controlFace(true, true) == ControlFace::LatchedPressed;
  return ok;
}

bool keyGlyphSelfTest() {
  const char* lower = "abcdefghijklmnopqrstuvwxyz";
  const char* upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  for (int i = 0; lower[i] != '\0'; ++i) {
    if (keyGlyph(lower[i], false) != lower[i] || keyGlyph(lower[i], true) != upper[i]) {
      return false;
    }
  }
  return keyGlyph('0', true) == '0' && keyGlyph('-', true) == '-' && keyGlyph('/', true) == '/' &&
         keyGlyph('@', true) == '@';
}

bool maskPasswordSelfTest() {
  char masked[8];
  masked[0] = 'p';
  maskPassword(masked, sizeof(masked), 4);
  if (masked[0] != '*' || masked[1] != '*' || masked[2] != '*' || masked[3] != '*' || masked[4] != '\0') {
    return false;
  }
  maskPassword(masked, sizeof(masked), 0);
  return masked[0] == '\0';
}
