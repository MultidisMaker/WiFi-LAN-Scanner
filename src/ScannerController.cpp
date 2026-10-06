#include "ScannerController.h"

#include "BoardConfig.h"
#include "FakeDiscovery.h"
#include "ScanClock.h"

#ifdef ARDUINO
#include <Arduino.h>
#endif

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
  const uint32_t now = scanNow();
  if (state_ == ScanState::Scanning && next == ScanState::Paused) {
    pauseBeganMs_ = now;
    pauseOpen_ = true;
  }
  if (state_ == ScanState::Paused && next == ScanState::Scanning && pauseOpen_) {
    pausedMs_ += now - pauseBeganMs_;
    pauseOpen_ = false;
  }
  if (elapsedOpen_ && (next == ScanState::Stopping || next == ScanState::Complete || next == ScanState::Idle)) {
    uint32_t frozenPause = pausedMs_;
    if (pauseOpen_) {
      frozenPause += now - pauseBeganMs_;
    }
    pausedMs_ = frozenPause;
    pauseOpen_ = false;
    elapsedOpen_ = false;
  }
  if (next == ScanState::Starting) {
    originMs_ = now;
    pausedMs_ = 0;
    pauseOpen_ = false;
    elapsedOpen_ = true;
  }
  state_ = next;
  enteredMs_ = now;
}

ScanState ScannerController::state() const { return state_; }

void ScannerController::setBackend(DiscoveryBackend* backend) { backend_ = backend; }

void ScannerController::holdFacts(bool hold) { factsHeld_ = hold; }

void ScannerController::armConnectedFacts(const NetFacts& facts) {
  if (factsHeld_) {
    return;
  }
  const bool identityChanged = !connected_ || armed_.prefix != facts.prefix || !ipv4Equal(armed_.network, facts.network);
  connected_ = true;
  armed_ = facts;
  // A custom start belongs to one joined subnet. Reconnecting, or joining a
  // different network, returns to Automatic. The batch size stays.
  if (identityChanged) {
    window_.mode = RangeMode::Automatic;
    window_.useOrigin = false;
  }
}

bool ScannerController::setAutomatic() {
  window_.mode = RangeMode::Automatic;
  window_.useOrigin = false;
  return true;
}

bool ScannerController::acceptsCustomStart(const Ipv4& start) const {
  if (!connected_ || !armed_.valid || !addressLimitOk(window_.limit)) {
    return false;
  }
  AddressWindow trial = window_;
  trial.mode = RangeMode::Custom;
  trial.useOrigin = true;
  trial.origin = start;
  CandidatePlan plan;
  RangePreview preview;
  previewAddressRange(armed_, trial, plan, preview);
  return preview.valid;
}

bool ScannerController::setCustomStart(const Ipv4& start) {
  if (!connected_ || !armed_.valid || !addressLimitOk(window_.limit)) {
    return false;
  }
  AddressWindow trial = window_;
  trial.mode = RangeMode::Custom;
  trial.useOrigin = true;
  trial.origin = start;
  CandidatePlan plan;
  RangePreview preview;
  previewAddressRange(armed_, trial, plan, preview);
  if (!preview.valid) {
    return false;
  }
  window_ = trial;
  return true;
}

bool ScannerController::setLimit(uint16_t limit) {
  if (!addressLimitOk(limit)) {
    return false;
  }
  window_.limit = limit;
  return true;
}

bool ScannerController::windowNext() {
  const RangePreview now = preview();
  if (!now.valid || !now.canNext) {
    return false;
  }
  AddressWindow trial = window_;
  trial.mode = armed_.usableHosts > kCandidateCap && window_.mode == RangeMode::Automatic ? RangeMode::Automatic
                                                                                           : RangeMode::Custom;
  trial.useOrigin = true;
  trial.origin = now.nextOrigin;
  CandidatePlan plan;
  RangePreview next;
  previewAddressRange(armed_, trial, plan, next);
  if (!next.valid) {
    return false;
  }
  window_ = trial;
  return true;
}

bool ScannerController::windowPrev() {
  const RangePreview now = preview();
  if (!now.canPrev) {
    return false;
  }
  if (now.prevAutomatic) {
    return setAutomatic();
  }
  AddressWindow trial = window_;
  trial.mode = armed_.usableHosts > kCandidateCap && window_.mode == RangeMode::Automatic ? RangeMode::Automatic
                                                                                           : RangeMode::Custom;
  trial.useOrigin = true;
  trial.origin = now.prevOrigin;
  CandidatePlan plan;
  RangePreview prev;
  previewAddressRange(armed_, trial, plan, prev);
  if (!prev.valid) {
    return false;
  }
  window_ = trial;
  return true;
}

RangePreview ScannerController::preview() const {
  CandidatePlan plan;
  RangePreview preview;
  if (!connected_) {
    preview.mode = window_.mode;
    preview.limit = window_.limit;
    preview.reason[0] = 'o';
    preview.reason[1] = 'f';
    preview.reason[2] = 'f';
    preview.reason[3] = 'l';
    preview.reason[4] = 'i';
    preview.reason[5] = 'n';
    preview.reason[6] = 'e';
    preview.reason[7] = '\0';
    return preview;
  }
  previewAddressRange(armed_, window_, plan, preview);
  return preview;
}

void ScannerController::armDisconnected() {
  if (factsHeld_) {
    return;
  }
  connected_ = false;
  armed_ = NetFacts();
}

void ScannerController::start() {
  if (state_ != ScanState::Idle && state_ != ScanState::Complete) {
    return;
  }
  if (!connected_ || !armed_.valid || backend_ == nullptr) {
    return;
  }
  RangePreview chosen;
  previewAddressRange(armed_, window_, plan_, chosen);
  if (!chosen.valid || plan_.count == 0 || plan_.count > kCandidateCap) {
    plan_ = CandidatePlan();
    return;
  }
  inventory_.clear();
  cursor_ = 0;
  processed_ = 0;
  probeActive_ = false;
  hasCurrent_ = false;
  hasLast_ = false;
  backend_->reset();
  enter(ScanState::Starting);
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
    if (backend_ != nullptr) {
      backend_->cancel();
    }
    probeActive_ = false;
    hasCurrent_ = false;
    enter(ScanState::Stopping);
  }
}

void ScannerController::acknowledge() {
  if (state_ == ScanState::Complete) {
    enter(ScanState::Idle);
  }
}

void ScannerController::reset() {
  if (backend_ != nullptr) {
    backend_->cancel();
    backend_->reset();
  }
  inventory_.clear();
  plan_ = CandidatePlan();
  cursor_ = 0;
  processed_ = 0;
  probeActive_ = false;
  hasCurrent_ = false;
  hasLast_ = false;
  enter(ScanState::Idle);
}

void ScannerController::finishProbe(const ProbeView& view) {
  if (view.status == ProbeStatus::Observed) {
    const uint32_t seen = elapsedMs();
    inventory_.observe(current_, view.evidence, view.hasMac, view.mac, view.hasLatency, view.latencyMs, seen,
                       view.method);
  }
  last_ = current_;
  hasLast_ = true;
  hasCurrent_ = false;
  probeActive_ = false;
  if (processed_ < plan_.count) {
    ++processed_;
  }
  ++cursor_;
}

void ScannerController::serviceDiscovery() {
  if (backend_ == nullptr || state_ != ScanState::Scanning) {
    return;
  }
  if (!probeActive_) {
    if (cursor_ >= plan_.count) {
      enter(ScanState::Complete);
      return;
    }
    current_ = plan_.address[cursor_];
    hasCurrent_ = true;
    backend_->startProbe(current_, scanNow());
    probeActive_ = true;
    return;
  }
  const ProbeView view = backend_->poll(scanNow());
  if (view.status == ProbeStatus::Pending || view.status == ProbeStatus::Idle) {
    return;
  }
  finishProbe(view);
  if (cursor_ >= plan_.count) {
    enter(ScanState::Complete);
  }
}

void ScannerController::loop() {
  const uint32_t elapsed = scanNow() - enteredMs_;
  if (state_ == ScanState::Starting && elapsed >= kScannerTransitionMs) {
    enter(ScanState::Scanning);
  } else if (state_ == ScanState::Stopping && elapsed >= kScannerTransitionMs) {
    enter(ScanState::Complete);
  }
  if (state_ == ScanState::Scanning) {
    serviceDiscovery();
  }
}

uint16_t ScannerController::candidateCount() const { return plan_.count; }
uint16_t ScannerController::processedCount() const { return processed_; }
uint16_t ScannerController::observedCount() const { return inventory_.count(); }
bool ScannerController::hasCurrent() const { return hasCurrent_; }
Ipv4 ScannerController::currentAddress() const { return current_; }
bool ScannerController::hasLast() const { return hasLast_; }
Ipv4 ScannerController::lastAddress() const { return last_; }

uint32_t ScannerController::elapsedMs() const {
  if (!elapsedOpen_ && state_ != ScanState::Starting && state_ != ScanState::Scanning && state_ != ScanState::Paused) {
    if (state_ == ScanState::Idle) {
      return 0;
    }
    uint32_t frozenPause = pausedMs_;
    const uint32_t end = enteredMs_;
    return end >= originMs_ + frozenPause ? end - originMs_ - frozenPause : 0;
  }
  const uint32_t now = scanNow();
  uint32_t frozenPause = pausedMs_;
  if (pauseOpen_) {
    frozenPause += now - pauseBeganMs_;
  }
  return now >= originMs_ + frozenPause ? now - originMs_ - frozenPause : 0;
}

const ObservedHost* ScannerController::hostAt(uint16_t index) const { return inventory_.at(index); }
const ObservedHost* ScannerController::newest() const { return inventory_.newest(); }

NameApply ScannerController::rememberName(const Ipv4& ip, const char* raw, NameSource source) {
  return inventory_.rememberName(ip, raw, source);
}

void ScannerController::enrichManufacturer(uint16_t index, const OuiTable& table) {
  inventory_.enrichManufacturer(index, table);
}

bool ScannerController::selfTest() {
  DiscoveryBackend* previous = backend_;
  const bool wasConnected = connected_;
  const NetFacts previousFacts = armed_;
  FakeDiscoveryBackend quiet;
  quiet.setWaitMs(60000);
  setBackend(&quiet);
  armConnectedFacts(deriveNetFacts(ipv4(192, 168, 0, 11), ipv4(255, 255, 255, 0), ipv4(192, 168, 0, 1),
                                   ipv4(192, 168, 0, 1), ipv4(0, 0, 0, 0)));
  start();
  bool ok = state_ == ScanState::Starting;
  enteredMs_ = scanNow() - static_cast<uint32_t>(kScannerTransitionMs);
  loop();
  ok = ok && state_ == ScanState::Scanning;
  pause();
  ok = ok && state_ == ScanState::Paused;
  resume();
  ok = ok && state_ == ScanState::Scanning;
  stop();
  ok = ok && state_ == ScanState::Stopping;
  enteredMs_ = scanNow() - static_cast<uint32_t>(kScannerTransitionMs);
  loop();
  ok = ok && state_ == ScanState::Complete;
  acknowledge();
  ok = ok && state_ == ScanState::Idle;
  reset();
  ok = ok && state_ == ScanState::Idle && observedCount() == 0;
  setBackend(previous);
  if (wasConnected) {
    armConnectedFacts(previousFacts);
  } else {
    armDisconnected();
  }
#ifdef ARDUINO
  Serial.printf("WLS scanner-selftest=%s discovery=local-arp\n", ok ? "ok" : "fail");
#endif
  return ok;
}
