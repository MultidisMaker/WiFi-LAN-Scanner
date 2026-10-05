#include "CandidatePlan.h"

#include "BoardConfig.h"

namespace {
uint32_t pack(const Ipv4& ip) {
  return (uint32_t(ip.octet[0]) << 24) | (uint32_t(ip.octet[1]) << 16) | (uint32_t(ip.octet[2]) << 8) |
         uint32_t(ip.octet[3]);
}

Ipv4 unpack(uint32_t value) {
  return ipv4(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
              static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
}

void sortPlan(CandidatePlan& plan) {
  for (uint16_t i = 1; i < plan.count; ++i) {
    const Ipv4 item = plan.address[i];
    const uint32_t packed = pack(item);
    uint16_t j = i;
    while (j > 0 && pack(plan.address[j - 1]) > packed) {
      plan.address[j] = plan.address[j - 1];
      --j;
    }
    plan.address[j] = item;
  }
}
}

void buildCandidatePlanInto(CandidatePlan& plan, const NetFacts& facts) {
  plan.valid = false;
  plan.capped = false;
  plan.gatewayIncluded = false;
  plan.gatewayForced = false;
  plan.eligibleCount = 0;
  plan.count = 0;
  if (!facts.valid || facts.usableHosts == 0) {
    return;
  }
  const uint32_t network = pack(facts.network);
  const uint32_t broadcast = pack(facts.broadcast);
  const uint32_t self = pack(facts.address);
  const uint32_t gateway = pack(facts.gateway);
  if (broadcast <= network + 1) {
    return;
  }
  uint32_t eligible = facts.usableHosts;
  const bool selfInside = self > network && self < broadcast;
  if (selfInside && eligible > 0) {
    --eligible;
  }
  for (uint32_t cursor = network + 1; cursor < broadcast && plan.count < kCandidateCap; ++cursor) {
    if (cursor == self) {
      continue;
    }
    plan.address[plan.count++] = unpack(cursor);
    if (cursor == 0xFFFFFFFEUL) {
      break;
    }
  }
  plan.eligibleCount = eligible;
  plan.capped = eligible > kCandidateCap;
  const bool gatewayEligible = gateway > network && gateway < broadcast && gateway != self;
  if (gatewayEligible) {
    bool present = false;
    for (uint16_t i = 0; i < plan.count; ++i) {
      if (pack(plan.address[i]) == gateway) {
        present = true;
        break;
      }
    }
    if (!present && plan.capped && plan.count > 0) {
      plan.address[plan.count - 1] = unpack(gateway);
      plan.gatewayForced = true;
      sortPlan(plan);
      present = true;
    }
    plan.gatewayIncluded = present;
  }
  plan.valid = plan.count > 0;
}

CandidatePlan buildCandidatePlan(const NetFacts& facts) {
  CandidatePlan plan;
  buildCandidatePlanInto(plan, facts);
  return plan;
}
