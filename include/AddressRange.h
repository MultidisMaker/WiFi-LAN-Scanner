#pragma once

#include <stddef.h>
#include <stdint.h>

#include "CandidatePlan.h"

// One scan batch never exceeds 256 candidates. Automatic keeps the existing
// low-window plan, including gateway replacement. Custom Start and later
// windows are session state: a joined-network change drops them, because a
// stored start address from another subnet is not safe to reuse. The batch
// size is the only range preference that may be stored.

enum class RangeMode : uint8_t { Automatic = 0, Custom = 1 };

struct AddressWindow {
  RangeMode mode = RangeMode::Automatic;
  uint16_t limit = 256;
  bool useOrigin = false;
  Ipv4 origin = {};
};

struct RangePreview {
  bool valid = false;
  bool clamped = false;
  bool gatewayIncluded = false;
  bool gatewayForced = false;
  bool canNext = false;
  bool canPrev = false;
  bool prevAutomatic = false;
  RangeMode mode = RangeMode::Automatic;
  uint16_t limit = 256;
  uint16_t count = 0;
  Ipv4 start = {};
  Ipv4 end = {};
  Ipv4 nextOrigin = {};
  Ipv4 prevOrigin = {};
  char reason[16] = {};
};

bool addressLimitOk(uint16_t limit);
bool parseIpv4(const char* text, Ipv4& out);
void previewAddressRange(const NetFacts& facts, const AddressWindow& window, CandidatePlan& plan, RangePreview& preview);
int formatRangeMarker(char* out, size_t cap, const RangePreview& preview);
