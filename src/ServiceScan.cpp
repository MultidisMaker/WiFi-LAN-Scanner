#include "ServiceScan.h"

#include <stdio.h>
#include <string.h>

ServiceConnectBackend::~ServiceConnectBackend() = default;

namespace {

ServiceProbeClass classify(ServiceConnectStatus status) {
  switch (status) {
    case ServiceConnectStatus::Open:
      return ServiceProbeClass::Open;
    case ServiceConnectStatus::Closed:
      return ServiceProbeClass::Closed;
    case ServiceConnectStatus::Timeout:
      return ServiceProbeClass::Timeout;
    case ServiceConnectStatus::Error:
      return ServiceProbeClass::Error;
    case ServiceConnectStatus::Pending:
      return ServiceProbeClass::None;
  }
  return ServiceProbeClass::Error;
}

char classLetter(ServiceProbeClass outcome) {
  switch (outcome) {
    case ServiceProbeClass::Open:
      return 'o';
    case ServiceProbeClass::Closed:
      return 'c';
    case ServiceProbeClass::Timeout:
      return 't';
    case ServiceProbeClass::Error:
      return 'e';
    case ServiceProbeClass::None:
      return '\0';
  }
  return '\0';
}

bool formatPorts(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result, char pair, char sep) {
  if (out == nullptr || cap == 0) {
    return false;
  }
  out[0] = '\0';
  if (result == nullptr || result->tested == 0) {
    return true;
  }
  size_t used = 0;
  const uint8_t count = result->tested < kServicePortCap ? result->tested : kServicePortCap;
  for (uint8_t i = 0; i < count; ++i) {
    const char letter = classLetter(result->state[i]);
    if (letter == '\0') {
      continue;
    }
    const uint16_t port = serviceProfilePort(profile, i);
    char piece[16];
    const int n = snprintf(piece, sizeof(piece), "%u%c%c", port, pair, letter);
    if (n < 0 || static_cast<size_t>(n) >= sizeof(piece)) {
      out[0] = '\0';
      return false;
    }
    if (used != 0) {
      if (used + 1 >= cap) {
        out[0] = '\0';
        return false;
      }
      out[used++] = sep;
    }
    if (used + static_cast<size_t>(n) >= cap) {
      out[0] = '\0';
      return false;
    }
    memcpy(out + used, piece, static_cast<size_t>(n));
    used += static_cast<size_t>(n);
    out[used] = '\0';
  }
  return true;
}

}  // namespace

void ServiceScan::setBackend(ServiceConnectBackend* backend) { backend_ = backend; }

void ServiceScan::setProfile(ServiceProfile profile) {
  if (run_ == ServiceRun::Running || run_ == ServiceRun::Paused) {
    return;
  }
  profile_ = profile;
}

ServiceProfile ServiceScan::profile() const { return armedProfile_; }

ServiceRun ServiceScan::run() const { return run_; }

void ServiceScan::clearResults() {
  active_ = false;
  portCount_ = 0;
  targetCount_ = 0;
  hostCursor_ = 0;
  portCursor_ = 0;
  planned_ = 0;
  completed_ = 0;
  started_ = 0;
  cancelled_ = 0;
  openHosts_ = 0;
  openPorts_ = 0;
  closedCount_ = 0;
  timeoutCount_ = 0;
  errorCount_ = 0;
  probeSerial_ = 0;
  hasLast_ = false;
  lastIp_ = Ipv4();
  lastPort_ = 0;
  lastClass_ = ServiceProbeClass::None;
  for (uint16_t i = 0; i < HostInventory::kCap; ++i) {
    targets_[i] = 0;
    results_[i] = ServiceHostResult();
  }
}

void ServiceScan::reset() {
  if (active_ && backend_ != nullptr) {
    backend_->cancel();
  }
  clearResults();
  run_ = ServiceRun::Idle;
}

bool ServiceScan::arm(const HostInventory& inventory, uint32_t nowMs) {
  (void)nowMs;
  if (backend_ == nullptr || run_ != ServiceRun::Idle) {
    return false;
  }
  clearResults();
  armedProfile_ = profile_;
  portCount_ = serviceProfilePortCount(armedProfile_);
  if (portCount_ > kServicePortCap) {
    portCount_ = kServicePortCap;
  }
  const uint16_t hosts = inventory.count();
  for (uint16_t i = 0; i < hosts && targetCount_ < HostInventory::kCap; ++i) {
    const ObservedHost* host = inventory.at(i);
    if (host != nullptr && host->hasMac) {
      targets_[targetCount_++] = i;
    }
  }
  planned_ = static_cast<uint16_t>(targetCount_ * portCount_);
  if (targetCount_ == 0 || portCount_ == 0) {
    run_ = ServiceRun::Complete;
    return true;
  }
  run_ = ServiceRun::Running;
  return true;
}

void ServiceScan::pause() {
  if (run_ == ServiceRun::Running) {
    run_ = ServiceRun::Paused;
  }
}

void ServiceScan::resume() {
  if (run_ == ServiceRun::Paused) {
    run_ = ServiceRun::Running;
  }
}

void ServiceScan::stop() {
  if (run_ != ServiceRun::Running && run_ != ServiceRun::Paused) {
    return;
  }
  if (active_ && backend_ != nullptr) {
    backend_->cancel();
    ++cancelled_;
  }
  active_ = false;
  run_ = ServiceRun::Stopped;
}

void ServiceScan::record(ServiceConnectStatus status, const Ipv4& ip, uint16_t port) {
  if (hostCursor_ >= targetCount_) {
    return;
  }
  const uint16_t index = targets_[hostCursor_];
  if (index >= HostInventory::kCap || portCursor_ >= portCount_ || portCursor_ >= kServicePortCap) {
    return;
  }
  ServiceHostResult& slot = results_[index];
  if (slot.tested != portCursor_) {
    return;
  }
  const ServiceProbeClass outcome = classify(status);
  slot.state[slot.tested] = outcome;
  slot.tested = static_cast<uint8_t>(slot.tested + 1);
  if (outcome == ServiceProbeClass::Open) {
    slot.openCount = static_cast<uint8_t>(slot.openCount + 1);
    ++openPorts_;
    if (slot.openCount == 1) {
      ++openHosts_;
    }
  } else if (outcome == ServiceProbeClass::Closed) {
    ++closedCount_;
  } else if (outcome == ServiceProbeClass::Timeout) {
    ++timeoutCount_;
  } else {
    ++errorCount_;
  }
  ++completed_;
  ++probeSerial_;
  lastIp_ = ip;
  lastPort_ = port;
  lastClass_ = outcome;
  hasLast_ = true;
  portCursor_ = static_cast<uint8_t>(portCursor_ + 1);
}

void ServiceScan::startNext(uint32_t nowMs, const HostInventory& inventory) {
  if (backend_ == nullptr) {
    run_ = ServiceRun::Stopped;
    return;
  }
  while (hostCursor_ < targetCount_) {
    if (portCursor_ >= portCount_) {
      ++hostCursor_;
      portCursor_ = 0;
      continue;
    }
    const uint16_t index = targets_[hostCursor_];
    const ObservedHost* host = inventory.at(index);
    if (host == nullptr || !host->hasMac) {
      active_ = false;
      run_ = ServiceRun::Stopped;
      return;
    }
    const uint16_t port = serviceProfilePort(armedProfile_, portCursor_);
    backend_->start(host->ip, port, nowMs, serviceProfileTimeoutMs(armedProfile_));
    active_ = true;
    ++started_;
    return;
  }
  active_ = false;
  run_ = ServiceRun::Complete;
}

void ServiceScan::loop(uint32_t nowMs, const HostInventory& inventory) {
  if (run_ != ServiceRun::Running) {
    return;
  }
  if (active_) {
    if (backend_ == nullptr) {
      active_ = false;
      run_ = ServiceRun::Stopped;
      return;
    }
    const ServiceConnectStatus status = backend_->poll(nowMs);
    if (status == ServiceConnectStatus::Pending) {
      return;
    }
    if (hostCursor_ >= targetCount_) {
      active_ = false;
      run_ = ServiceRun::Stopped;
      return;
    }
    const ObservedHost* host = inventory.at(targets_[hostCursor_]);
    const uint16_t port = serviceProfilePort(armedProfile_, portCursor_);
    const Ipv4 ip = host != nullptr ? host->ip : Ipv4();
    active_ = false;
    record(status, ip, port);
    (void)port;
  }
  if (run_ != ServiceRun::Running) {
    return;
  }
  startNext(nowMs, inventory);
}

bool ServiceScan::idle() const {
  return run_ == ServiceRun::Idle || run_ == ServiceRun::Complete || run_ == ServiceRun::Stopped;
}

bool ServiceScan::running() const { return run_ == ServiceRun::Running; }

bool ServiceScan::paused() const { return run_ == ServiceRun::Paused; }

uint16_t ServiceScan::planned() const { return planned_; }
uint16_t ServiceScan::completed() const { return completed_; }
uint16_t ServiceScan::startedCount() const { return started_; }
uint16_t ServiceScan::cancelledCount() const { return cancelled_; }
uint16_t ServiceScan::targetCount() const { return targetCount_; }
uint16_t ServiceScan::openHosts() const { return openHosts_; }
uint16_t ServiceScan::openPorts() const { return openPorts_; }
uint16_t ServiceScan::closedCount() const { return closedCount_; }
uint16_t ServiceScan::timeoutCount() const { return timeoutCount_; }
uint16_t ServiceScan::errorCount() const { return errorCount_; }
uint16_t ServiceScan::probeSerial() const { return probeSerial_; }

bool ServiceScan::lastProbe(Ipv4& ip, uint16_t& port, ServiceProbeClass& outcome) const {
  if (!hasLast_) {
    return false;
  }
  ip = lastIp_;
  port = lastPort_;
  outcome = lastClass_;
  return true;
}

const ServiceHostResult* ServiceScan::resultAt(uint16_t inventoryIndex) const {
  if (inventoryIndex >= HostInventory::kCap) {
    return nullptr;
  }
  return &results_[inventoryIndex];
}

char servicePhaseLetter(const char* token) {
  if (token != nullptr) {
    if (strcmp(token, "disc") == 0) {
      return 'd';
    }
    if (strcmp(token, "name") == 0) {
      return 'n';
    }
    if (strcmp(token, "svc") == 0) {
      return 's';
    }
    if (strcmp(token, "done") == 0) {
      return 'c';
    }
    if (strcmp(token, "stop") == 0) {
      return 'x';
    }
  }
  return 'i';
}

bool formatServiceField(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result) {
  return formatPorts(out, cap, profile, result, ':', '|');
}

bool formatServiceWirePorts(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result) {
  return formatPorts(out, cap, profile, result, '=', ';');
}
