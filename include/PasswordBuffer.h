#pragma once

#include "BoardConfig.h"

// Typed password state shared by firmware and host tests. Callers must not log data().
class PasswordBuffer {
 public:
  bool typeChar(char c);
  void backspace();
  void toggleShift();
  void setShift(bool on);
  bool shiftOn() const { return shift_; }
  int length() const { return length_; }
  const char* data() const { return data_; }
  void clear();
  // Types one synthetic character, toggles Shift twice, and clears the buffer.
  // Returns whether the character survived. Does not print it.
  bool preservedAcrossShift();

 private:
  char data_[kMaxPassLen + 1] = {};
  int length_ = 0;
  bool shift_ = false;
};
