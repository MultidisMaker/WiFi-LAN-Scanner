#pragma once

#include "NetMath.h"

// Active discovery examines at most kFutureScanHostCap eligible addresses.
// Eligible addresses are the usable hosts except the station itself. The gateway
// is eligible when it is one of those hosts. A subnet that fits in one batch
// keeps its existing low window. A larger subnet uses the address-count-aligned
// block that contains the station. The gateway is added, not substituted, when
// that block still has room under the batch cap. The list stays ascending.
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
