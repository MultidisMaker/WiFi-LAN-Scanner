#include "PasswordBuffer.h"

#include <string.h>

bool PasswordBuffer::typeChar(char c) {
  if (length_ >= kMaxPassLen || c == '\0') {
    return false;
  }
  data_[length_++] = c;
  data_[length_] = '\0';
  return true;
}

void PasswordBuffer::backspace() {
  if (length_ <= 0) {
    return;
  }
  data_[--length_] = '\0';
}

void PasswordBuffer::toggleShift() { shift_ = !shift_; }

void PasswordBuffer::setShift(bool on) { shift_ = on; }

void PasswordBuffer::clear() {
  memset(data_, 0, sizeof(data_));
  length_ = 0;
}

bool PasswordBuffer::preservedAcrossShift() {
  if (length_ != 0 || shift_ || data_[0] != '\0') {
    return false;
  }
  typeChar('a');
  const bool typed = length_ == 1 && data_[0] == 'a' && data_[1] == '\0';
  toggleShift();
  const bool preserved = typed && shift_ && length_ == 1 && data_[0] == 'a';
  toggleShift();
  const bool restored = preserved && !shift_ && data_[0] == 'a';
  clear();
  shift_ = false;
  return restored && length_ == 0 && !shift_ && data_[0] == '\0';
}
