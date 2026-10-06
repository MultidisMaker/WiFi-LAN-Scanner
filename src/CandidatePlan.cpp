#include "CandidatePlan.h"

#include "AddressRange.h"

void buildCandidatePlanInto(CandidatePlan& plan, const NetFacts& facts) {
  AddressWindow window;
  RangePreview preview;
  previewAddressRange(facts, window, plan, preview);
}

CandidatePlan buildCandidatePlan(const NetFacts& facts) {
  CandidatePlan plan;
  buildCandidatePlanInto(plan, facts);
  return plan;
}
