#pragma once

#include <stdint.h>

#include "CandidatePlan.h"
#include "DiscoveryBackend.h"
#include "HostInventory.h"

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
  void setBackend(DiscoveryBackend* backend);
  void armConnectedFacts(const NetFacts& facts);
  void armDisconnected();
  void start();
  void pause();
  void resume();
  void stop();
  void acknowledge();
  void reset();
  void loop();
  bool selfTest();

  uint16_t candidateCount() const;
  uint16_t processedCount() const;
  uint16_t observedCount() const;
  bool hasCurrent() const;
  Ipv4 currentAddress() const;
  bool hasLast() const;
  Ipv4 lastAddress() const;
  uint32_t elapsedMs() const;
  const ObservedHost* hostAt(uint16_t index) const;
  const ObservedHost* newest() const;
  NameApply rememberName(const Ipv4& ip, const char* raw, NameSource source);

 private:
  ScanState state_ = ScanState::Idle;
  uint32_t enteredMs_ = 0;
  bool connected_ = false;
  NetFacts armed_ = {};
  DiscoveryBackend* backend_ = nullptr;
  CandidatePlan plan_ = {};
  HostInventory inventory_;
  uint16_t cursor_ = 0;
  uint16_t processed_ = 0;
  bool probeActive_ = false;
  bool hasCurrent_ = false;
  bool hasLast_ = false;
  Ipv4 current_ = {};
  Ipv4 last_ = {};
  uint32_t originMs_ = 0;
  uint32_t pausedMs_ = 0;
  uint32_t pauseBeganMs_ = 0;
  bool pauseOpen_ = false;
  bool elapsedOpen_ = false;

  void enter(ScanState next);
  void serviceDiscovery();
  void finishProbe(const ProbeView& view);
};
