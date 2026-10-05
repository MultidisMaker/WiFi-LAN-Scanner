#pragma once

#include "NetMath.h"

// Active discovery examines at most kFutureScanHostCap eligible addresses.
// Eligible addresses are the usable hosts (network+1 through broadcast-1) except
// the station itself. The gateway is eligible when it is one of those hosts.
// When more than 256 are eligible, the plan keeps the 256 lowest. If the
// gateway is eligible and sits above that window, it replaces the highest
// selected address and the list is restored to ascending order.
static constexpr uint16_t kCandidateCap = 256;

struct CandidatePlan {
  bool valid = false;
  bool capped = false;
  bool gatewayIncluded = false;
  bool gatewayForced = false;
  uint32_t eligibleCount = 0;
  uint16_t count = 0;
  Ipv4 address[kCandidateCap] = {};
};

void buildCandidatePlanInto(CandidatePlan& plan, const NetFacts& facts);
CandidatePlan buildCandidatePlan(const NetFacts& facts);
