#include "DnsPtrEnricher.h"

#include <WiFi.h>
#include <WiFiUdp.h>

#include <stdio.h>
#include <string.h>

#include "DnsPtr.h"
#include "NameRecord.h"
#include "ScannerController.h"

namespace {

constexpr uint16_t kAttemptCap = 256;
constexpr uint32_t kPtrTimeoutMs = 500;

WiFiUDP udp_;
bool udpOpen_ = false;
bool waiting_ = false;
bool summaryLogged_ = false;
uint16_t nextId_ = 1;
uint16_t inflightId_ = 0;
uint32_t queryStart_ = 0;
Ipv4 pendingIp_ = {};
Ipv4 attempts_[kAttemptCap];
uint16_t attemptCount_ = 0;
uint16_t resolved_ = 0;
uint16_t nxdomain_ = 0;
uint16_t noname_ = 0;
uint16_t timeout_ = 0;
uint16_t error_ = 0;

bool attempted(const Ipv4& ip) {
  for (uint16_t i = 0; i < attemptCount_; ++i) {
    if (ipv4Equal(attempts_[i], ip)) {
      return true;
    }
  }
  return false;
}

void noteAttempt(const Ipv4& ip) {
  if (attempted(ip) || attemptCount_ >= kAttemptCap) {
    return;
  }
  attempts_[attemptCount_++] = ip;
}

void closeUdp() {
  if (udpOpen_) {
    udp_.stop();
    udpOpen_ = false;
  }
  waiting_ = false;
}

void clearCycle() {
  closeUdp();
  attemptCount_ = 0;
  resolved_ = 0;
  nxdomain_ = 0;
  noname_ = 0;
  timeout_ = 0;
  error_ = 0;
  summaryLogged_ = false;
}

bool discoveryActive(ScanState phase) {
  return phase == ScanState::Starting || phase == ScanState::Scanning || phase == ScanState::Paused ||
         phase == ScanState::Stopping;
}

const ObservedHost* nextHost(const ScannerController& scanner) {
  for (uint16_t i = 0; i < scanner.observedCount(); ++i) {
    const ObservedHost* host = scanner.hostAt(i);
    if (host != nullptr && !attempted(host->ip)) {
      return host;
    }
  }
  return nullptr;
}

void logResult(const Ipv4& ip, const char* kind, uint32_t elapsed, const char* name) {
  char text[16];
  formatIpv4(ip, text, sizeof(text));
  if (name != nullptr && name[0] != '\0') {
    Serial.printf("WLS dns ptr ip=%s class=%s elapsed=%lu name=%s\n", text, kind, static_cast<unsigned long>(elapsed),
                  name);
    return;
  }
  Serial.printf("WLS dns ptr ip=%s class=%s elapsed=%lu\n", text, kind, static_cast<unsigned long>(elapsed));
}

void countKind(const char* kind) {
  if (strcmp(kind, "resolved") == 0 && resolved_ < 65535) {
    ++resolved_;
  } else if (strcmp(kind, "nxdomain") == 0 && nxdomain_ < 65535) {
    ++nxdomain_;
  } else if (strcmp(kind, "noname") == 0 && noname_ < 65535) {
    ++noname_;
  } else if (strcmp(kind, "timeout") == 0 && timeout_ < 65535) {
    ++timeout_;
  } else if (error_ < 65535) {
    ++error_;
  }
}

void finishSummary(const IPAddress& resolver) {
  if (summaryLogged_ || attemptCount_ == 0) {
    return;
  }
  Serial.printf("WLS dns summary resolver=%u.%u.%u.%u attempted=%u resolved=%u nxdomain=%u noname=%u timeout=%u error=%u\n",
                resolver[0], resolver[1], resolver[2], resolver[3], attemptCount_, resolved_, nxdomain_, noname_, timeout_,
                error_);
  summaryLogged_ = true;
}

bool startQuery(const Ipv4& ip, const IPAddress& resolver) {
  uint8_t packet[128];
  size_t used = 0;
  const uint16_t id = nextId_++;
  if (nextId_ == 0) {
    nextId_ = 1;
  }
  if (!buildPtrQuestion(ip, id, packet, sizeof(packet), &used)) {
    return false;
  }
  if (!udpOpen_) {
    udpOpen_ = udp_.begin(0);
  }
  if (!udpOpen_ || !udp_.beginPacket(resolver, 53) || udp_.write(packet, used) != used || !udp_.endPacket()) {
    closeUdp();
    return false;
  }
  pendingIp_ = ip;
  queryStart_ = millis();
  waiting_ = true;
  inflightId_ = id;
  return true;
}

void acceptPacket(ScannerController& scanner) {
  uint8_t packet[512];
  const int size = udp_.read(packet, sizeof(packet));
  const uint32_t elapsed = millis() - queryStart_;
  waiting_ = false;
  noteAttempt(pendingIp_);
  if (size <= 0) {
    countKind("error");
    logResult(pendingIp_, "error", elapsed, nullptr);
    return;
  }
  char name[128];
  PtrReply reply = PtrReply::Malformed;
  if (!parsePtrReply(packet, static_cast<size_t>(size), inflightId_, name, sizeof(name), &reply)) {
    reply = PtrReply::Malformed;
  }
  const char* kind = ptrReplyLabel(reply);
  if (reply == PtrReply::Resolved) {
    const NameApply applied = scanner.rememberName(pendingIp_, name, NameSource::ReverseDns);
    if (applied == NameApply::Rejected) {
      kind = "noname";
    }
  }
  countKind(kind);
  logResult(pendingIp_, kind, elapsed, reply == PtrReply::Resolved ? name : nullptr);
}

}  // namespace

void serviceDnsEnrichment(ScannerController& scanner) {
  const ScanState phase = scanner.state();
  if (discoveryActive(phase) || phase == ScanState::Idle) {
    if (discoveryActive(phase)) {
      clearCycle();
    } else {
      closeUdp();
    }
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    closeUdp();
    return;
  }
  const IPAddress resolver = WiFi.dnsIP();
  if (waiting_) {
    const int ready = udp_.parsePacket();
    if (ready > 0) {
      acceptPacket(scanner);
    } else if (millis() - queryStart_ >= kPtrTimeoutMs) {
      const uint32_t elapsed = millis() - queryStart_;
      waiting_ = false;
      noteAttempt(pendingIp_);
      countKind("timeout");
      logResult(pendingIp_, "timeout", elapsed, nullptr);
    } else {
      return;
    }
  }
  const ObservedHost* host = nextHost(scanner);
  if (host == nullptr) {
    finishSummary(resolver);
    closeUdp();
    return;
  }
  if (resolver == IPAddress(0, 0, 0, 0)) {
    noteAttempt(host->ip);
    countKind("error");
    logResult(host->ip, "error", 0, nullptr);
    return;
  }
  if (attemptCount_ == 0 && !summaryLogged_) {
    Serial.printf("WLS dns resolver=%u.%u.%u.%u\n", resolver[0], resolver[1], resolver[2], resolver[3]);
  }
  if (!startQuery(host->ip, resolver)) {
    noteAttempt(host->ip);
    countKind("error");
    logResult(host->ip, "error", 0, nullptr);
  }
}

bool dnsEnrichmentIdle(const ScannerController& scanner) {
  if (WiFi.status() != WL_CONNECTED || discoveryActive(scanner.state()) || scanner.state() != ScanState::Complete) {
    return true;
  }
  return !waiting_ && nextHost(scanner) == nullptr;
}

uint16_t dnsEnrichmentAttemptCount() { return attemptCount_; }
