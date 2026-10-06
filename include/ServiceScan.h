#pragma once

#include <stddef.h>
#include <stdint.h>

#include "HostInventory.h"
#include "ServiceProfile.h"

// One TCP connect at a time. The backend must not send or receive payload bytes.
enum class ServiceConnectStatus : uint8_t { Pending = 0, Open, Closed, Timeout, Error };

class ServiceConnectBackend {
 public:
  virtual ~ServiceConnectBackend();
  virtual void start(const Ipv4& ip, uint16_t port, uint32_t nowMs, uint32_t timeoutMs) = 0;
  virtual ServiceConnectStatus poll(uint32_t nowMs) = 0;
  virtual void cancel() = 0;
};

enum class ServiceProbeClass : uint8_t { None = 0, Open, Closed, Timeout, Error };

enum class ServiceRun : uint8_t { Idle = 0, Running, Paused, Stopped, Complete };

struct ServiceHostResult {
  uint8_t tested = 0;
  uint8_t openCount = 0;
  ServiceProbeClass state[kServicePortCap] = {};
};

// Fixed results for the 256-host inventory. Indexes match HostInventory.
class ServiceScan {
 public:
  void setBackend(ServiceConnectBackend* backend);
  void setProfile(ServiceProfile profile);
  ServiceProfile profile() const;
  ServiceRun run() const;

  void reset();
  // Snapshots MAC-bearing hosts. Zero targets completes with a zero plan.
  bool arm(const HostInventory& inventory, uint32_t nowMs);
  void pause();
  void resume();
  void stop();
  // Completes at most the in-flight attempt and starts at most one new attempt.
  void loop(uint32_t nowMs, const HostInventory& inventory);

  bool idle() const;
  bool running() const;
  bool paused() const;

  uint16_t planned() const;
  uint16_t completed() const;
  uint16_t startedCount() const;
  uint16_t cancelledCount() const;
  uint16_t targetCount() const;
  uint16_t openHosts() const;
  uint16_t openPorts() const;
  uint16_t closedCount() const;
  uint16_t timeoutCount() const;
  uint16_t errorCount() const;
  uint16_t probeSerial() const;
  bool lastProbe(Ipv4& ip, uint16_t& port, ServiceProbeClass& outcome) const;
  const ServiceHostResult* resultAt(uint16_t inventoryIndex) const;

 private:
  ServiceConnectBackend* backend_ = nullptr;
  ServiceProfile profile_ = ServiceProfile::Common;
  ServiceProfile armedProfile_ = ServiceProfile::Common;
  ServiceRun run_ = ServiceRun::Idle;
  uint8_t portCount_ = 0;
  uint16_t targets_[HostInventory::kCap] = {};
  uint16_t targetCount_ = 0;
  uint16_t hostCursor_ = 0;
  uint8_t portCursor_ = 0;
  bool active_ = false;
  uint16_t planned_ = 0;
  uint16_t completed_ = 0;
  uint16_t started_ = 0;
  uint16_t cancelled_ = 0;
  uint16_t openHosts_ = 0;
  uint16_t openPorts_ = 0;
  uint16_t closedCount_ = 0;
  uint16_t timeoutCount_ = 0;
  uint16_t errorCount_ = 0;
  uint16_t probeSerial_ = 0;
  bool hasLast_ = false;
  Ipv4 lastIp_ = {};
  uint16_t lastPort_ = 0;
  ServiceProbeClass lastClass_ = ServiceProbeClass::None;
  ServiceHostResult results_[HostInventory::kCap] = {};

  void clearResults();
  void record(ServiceConnectStatus status, const Ipv4& ip, uint16_t port);
  void startNext(uint32_t nowMs, const HostInventory& inventory);
};

// One letter for the STATE svc field: i d n s c x.
char servicePhaseLetter(const char* token);

// CSV cell. Empty when the host was not tested. Uses port:o|port:c|port:t|port:e.
bool formatServiceField(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result);

// Remote ports cell. Uses port=o;port=c;port=t;port=e.
bool formatServiceWirePorts(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result);
