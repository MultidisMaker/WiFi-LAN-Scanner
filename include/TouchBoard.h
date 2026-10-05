#pragma once

#include <stdint.h>

class TouchBoard;
TouchBoard& deviceTouch();

class TouchBoard {
 public:
  bool begin();
  bool ready() const;
  const char* model() const;
  // Current finger contact. Coordinates are panel pixels while down is true.
  bool readContact(bool& down, int& x, int& y);

 private:
  bool ready_ = false;
  char model_[24] = "absent";
};
