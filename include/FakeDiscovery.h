#pragma once

#include "DiscoveryBackend.h"

#include <string.h>

// Scripted backend for host tests and the synthetic HIL path. It does not transmit.
class FakeDiscoveryBackend : public DiscoveryBackend {
 public:
  static constexpr int kScriptCap = 16;

  void setWaitMs(uint32_t waitMs) { waitMs_ = waitMs; }

  void addObservation(const Ipv4& ip, bool hasMac, const uint8_t mac[6], bool hasLatency, uint32_t latencyMs) {
    if (scriptCount_ >= kScriptCap) {
      return;
    }
    Script& script = script_[scriptCount_++];
    script.ip = ip;
    script.hasMac = hasMac;
    if (hasMac && mac != nullptr) {
      memcpy(script.mac, mac, 6);
    }
    script.hasLatency = hasLatency;
    script.latencyMs = latencyMs;
  }

  void clearScripts() {
    scriptCount_ = 0;
    starts_ = 0;
    active_ = false;
  }

  void reset() override { active_ = false; }

  void startProbe(const Ipv4& target, uint32_t nowMs) override {
    active_ = true;
    target_ = target;
    startedMs_ = nowMs;
    ++starts_;
  }

  ProbeView poll(uint32_t nowMs) override {
    ProbeView view;
    view.method = methodName();
    if (!active_) {
      view.status = ProbeStatus::Idle;
      return view;
    }
    if (nowMs - startedMs_ < waitMs_) {
      view.status = ProbeStatus::Pending;
      return view;
    }
    const Script* script = find(target_);
    if (script == nullptr) {
      view.status = ProbeStatus::Unanswered;
      return view;
    }
    view.status = ProbeStatus::Observed;
    view.hasMac = script->hasMac;
    if (script->hasMac) {
      memcpy(view.mac, script->mac, 6);
      view.evidence = EvidenceRank::Neighbor;
    } else {
      view.evidence = EvidenceRank::Answered;
    }
    view.hasLatency = script->hasLatency;
    view.latencyMs = script->latencyMs;
    return view;
  }

  void cancel() override { active_ = false; }

  const char* methodName() const override { return "fake"; }

  int starts() const { return starts_; }

 private:
  struct Script {
    Ipv4 ip;
    bool hasMac = false;
    uint8_t mac[6] = {};
    bool hasLatency = false;
    uint32_t latencyMs = 0;
  };

  const Script* find(const Ipv4& ip) const {
    for (int i = 0; i < scriptCount_; ++i) {
      if (ipv4Equal(script_[i].ip, ip)) {
        return &script_[i];
      }
    }
    return nullptr;
  }

  Script script_[kScriptCap] = {};
  int scriptCount_ = 0;
  bool active_ = false;
  Ipv4 target_;
  uint32_t startedMs_ = 0;
  uint32_t waitMs_ = 0;
  int starts_ = 0;
};
