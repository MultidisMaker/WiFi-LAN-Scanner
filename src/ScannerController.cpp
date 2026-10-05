#include "ScannerController.h"

#include "BoardConfig.h"

const char* scanStateName(ScanState state) {
  switch (state) {
    case ScanState::Idle:
      return "IDLE";
    case ScanState::Starting:
      return "STARTING";
    case ScanState::Scanning:
      return "SCANNING";
    case ScanState::Paused:
      return "PAUSED";
    case ScanState::Stopping:
      return "STOPPING";
    case ScanState::Complete:
      return "COMPLETE";
  }
  return "UNKNOWN";
}

void ScannerController::enter(ScanState next) {
  state_ = next;
  enteredMs_ = millis();
}

ScanState ScannerController::state() const { return state_; }

void ScannerController::start() {
  if (state_ == ScanState::Idle || state_ == ScanState::Complete) {
    enter(ScanState::Starting);
  }
}

void ScannerController::pause() {
  if (state_ == ScanState::Scanning) {
    enter(ScanState::Paused);
  }
}

void ScannerController::resume() {
  if (state_ == ScanState::Paused) {
    enter(ScanState::Scanning);
  }
}

void ScannerController::stop() {
  if (state_ == ScanState::Scanning || state_ == ScanState::Paused || state_ == ScanState::Starting) {
    enter(ScanState::Stopping);
  }
}

void ScannerController::acknowledge() {
  if (state_ == ScanState::Complete) {
    enter(ScanState::Idle);
  }
}

void ScannerController::loop() {
  const unsigned long elapsed = millis() - enteredMs_;
  if (state_ == ScanState::Starting && elapsed >= kScannerTransitionMs) {
    enter(ScanState::Scanning);
  } else if (state_ == ScanState::Stopping && elapsed >= kScannerTransitionMs) {
    enter(ScanState::Complete);
  }
}

bool ScannerController::selfTest() {
  start();
  bool ok = state_ == ScanState::Starting;
  enteredMs_ = millis() - kScannerTransitionMs;
  loop();
  ok = ok && state_ == ScanState::Scanning;
  pause();
  ok = ok && state_ == ScanState::Paused;
  resume();
  ok = ok && state_ == ScanState::Scanning;
  stop();
  ok = ok && state_ == ScanState::Stopping;
  enteredMs_ = millis() - kScannerTransitionMs;
  loop();
  ok = ok && state_ == ScanState::Complete;
  acknowledge();
  ok = ok && state_ == ScanState::Idle;
  Serial.printf("WLS scanner-selftest=%s discovery=deferred\n", ok ? "ok" : "fail");
  return ok;
}
