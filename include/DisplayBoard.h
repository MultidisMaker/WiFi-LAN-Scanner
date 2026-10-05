#pragma once

#include <Arduino_GFX_Library.h>

class DisplayBoard;
DisplayBoard& deviceDisplay();

class DisplayBoard {
 public:
  bool begin();
  int width() const;
  int height() const;
  bool ready() const;
  Arduino_GFX& panel();

 private:
  bool ready_ = false;
  int width_ = 0;
  int height_ = 0;
};
