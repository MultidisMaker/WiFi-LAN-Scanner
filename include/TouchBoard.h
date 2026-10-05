#pragma once

#include <stdint.h>

class TouchBoard;
TouchBoard& deviceTouch();

class TouchBoard {
 public:
  bool begin();
  bool ready() const;
  const char* model() const;
  // Returns true once per new press. Coordinates are panel pixels.
  bool takePress(int& x, int& y);

 private:
  bool ready_ = false;
  bool wasDown_ = false;
  char model_[24] = "absent";
};
