#pragma once

#include <Arduino.h>

// Foundation only. SCANNING does not transmit ARP, ICMP, TCP, UDP, or any other host probe.
enum class ScanState : uint8_t {
  Idle,
  Starting,
  Scanning,
  Paused,
  Stopping,
  Complete
};

const char* scanStateName(ScanState state);

class ScannerController {
 public:
  ScanState state() const;
  void start();
  void pause();
  void resume();
  void stop();
  void acknowledge();
  void loop();
  bool selfTest();

 private:
  ScanState state_ = ScanState::Idle;
  unsigned long enteredMs_ = 0;
  void enter(ScanState next);
};
