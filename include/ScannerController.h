#pragma once

#include <stdint.h>

#include "AddressRange.h"
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

class ServiceScan;

class ScannerController {
 public:
  ScanState state() const;
  void setBackend(DiscoveryBackend* backend);
  void armConnectedFacts(const NetFacts& facts);
  void armDisconnected();
  // Test image only. While held, station refresh does not replace the armed facts.
  void holdFacts(bool hold);
  bool setAutomatic();
  bool acceptsCustomStart(const Ipv4& start) const;
  bool setCustomStart(const Ipv4& start);
  bool setLimit(uint16_t limit);
  bool windowNext();
  bool windowPrev();
  RangePreview preview() const;
  void start();
  void pause();
  void resume();
  void stop();
  void acknowledge();
  void reset();
  void loop();
  bool selfTest();
  void bindServiceScan(ServiceScan* scan);
  const ServiceScan* serviceScan() const;
  // Arms only while this controller is Complete and the scan is still idle.
  bool armServiceScan(uint32_t nowMs);
  void serviceLoop(uint32_t nowMs);

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
  void enrichManufacturer(uint16_t index, const OuiTable& table);

 private:
  ScanState state_ = ScanState::Idle;
  uint32_t enteredMs_ = 0;
  bool connected_ = false;
  bool factsHeld_ = false;
  NetFacts armed_ = {};
  AddressWindow window_ = {};
  DiscoveryBackend* backend_ = nullptr;
  ServiceScan* services_ = nullptr;
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
