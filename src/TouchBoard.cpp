#include "TouchBoard.h"

#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>

#include "BoardConfig.h"
#include "REG/CSTxxxConstants.h"

namespace {
TouchDrvCSTXXX gTouch;
}

bool TouchBoard::begin() {
  Wire.begin(kTouchSda, kTouchScl);
  gTouch.setPins(kTouchRst, kTouchIrq);
  uint8_t probe = 0xFF;
  ready_ = false;
  for (int attempt = 0; attempt < 6 && !ready_; ++attempt) {
    delay(150);
    Wire.beginTransmission(CST226SE_SLAVE_ADDRESS);
    probe = Wire.endTransmission();
    if (probe == 0) {
      ready_ = gTouch.begin(Wire, CST226SE_SLAVE_ADDRESS, kTouchSda, kTouchScl);
    }
  }
  if (ready_) {
    strncpy(model_, gTouch.getModelName(), sizeof(model_) - 1);
    model_[sizeof(model_) - 1] = '\0';
  } else {
    strncpy(model_, "absent", sizeof(model_) - 1);
  }
  Serial.printf("WLS touch probe=%u model=%s sda=%d scl=%d rst=%d irq=%d\n", probe, model_, kTouchSda,
                kTouchScl, kTouchRst, kTouchIrq);
  return ready_;
}

bool TouchBoard::ready() const { return ready_; }

const char* TouchBoard::model() const { return model_; }

bool TouchBoard::takePress(int& x, int& y) {
  if (!ready_) {
    return false;
  }
  int16_t xs[1] = {0};
  int16_t ys[1] = {0};
  const uint8_t points = gTouch.getPoint(xs, ys, 1);
  const bool down = points > 0;
  bool edge = false;
  if (down && !wasDown_) {
    x = xs[0];
    y = ys[0];
    edge = true;
  }
  wasDown_ = down;
  return edge;
}
