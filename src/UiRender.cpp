#include "UiRender.h"

#include <string.h>

int progressPercent(uint16_t processed, uint16_t candidates) {
  if (candidates == 0) {
    return 0;
  }
  if (processed >= candidates) {
    return 100;
  }
  return static_cast<int>((static_cast<uint32_t>(processed) * 100u) / static_cast<uint32_t>(candidates));
}

namespace {

bool textInsideOneRegion(int y, int textH) {
  static const uint32_t kBits[] = {UiRegionHeader, UiRegionWifiActions, UiRegionNetwork, UiRegionProgress,
                                    UiRegionLatest, UiRegionControls,    UiRegionFooter};
  for (uint32_t bit : kBits) {
    const UiRegionRect rect = uiRegionRect(bit);
    if (y >= rect.y && y + textH <= rect.y + rect.h) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool uiHostTextY(int cardY, int cardH, int lineIndex, int textH, int* outY) {
  if (outY == nullptr || cardH <= 0 || textH <= 0 || lineIndex < 0) {
    return false;
  }
  int y = cardY + 2;
  const int limit = cardY + cardH;
  for (int placed = 0; placed <= lineIndex; ++placed) {
    bool found = false;
    while (y + textH <= limit) {
      if (textInsideOneRegion(y, textH)) {
        found = true;
        break;
      }
      ++y;
    }
    if (!found) {
      return false;
    }
    if (placed == lineIndex) {
      *outY = y;
      return true;
    }
    y += textH + 2;
  }
  return false;
}

UiRegionRect uiRegionRect(uint32_t bit) {
  switch (bit) {
    case UiRegionHeader:
      return {0, 0, kPanelWidth, 70};
    case UiRegionWifiActions:
      return {0, 70, kPanelWidth, 76};
    case UiRegionNetwork:
      return {0, 146, kPanelWidth, 94};
    case UiRegionProgress:
      return {0, 240, kPanelWidth, 46};
    case UiRegionLatest:
      return {0, 286, kPanelWidth, 34};
    case UiRegionControls:
      return {0, 320, kPanelWidth, 140};
    case UiRegionFooter:
      return {0, 460, kPanelWidth, 20};
    default:
      return {0, 0, 0, 0};
  }
}

namespace {

uint32_t regionsTouched(int y, int h) {
  if (h <= 0) {
    return UiRegionAll;
  }
  static const uint32_t kBits[] = {UiRegionHeader, UiRegionWifiActions, UiRegionNetwork, UiRegionProgress,
                                    UiRegionLatest, UiRegionControls,    UiRegionFooter};
  uint32_t mask = 0;
  for (uint32_t bit : kBits) {
    const UiRegionRect rect = uiRegionRect(bit);
    if (y < rect.y + rect.h && y + h > rect.y) {
      mask |= bit;
    }
  }
  return mask == 0 ? UiRegionAll : mask;
}

}  // namespace

uint32_t dirtyRegions(const UiPaintFrame& prev, const UiPaintFrame& next) {
  if (prev.screen != next.screen || prev.page != next.page || prev.keyboardPage != next.keyboardPage ||
      prev.shift != next.shift || prev.passLen != next.passLen) {
    return UiRegionAll;
  }
  if ((next.screen == UiPaintScreen::Hosts || next.screen == UiPaintScreen::Results) && prev.listStamp != next.listStamp) {
    return UiRegionAll;
  }
  if (next.screen == UiPaintScreen::Settings &&
      (prev.profile != next.profile || prev.listStamp != next.listStamp)) {
    return UiRegionAll;
  }

  uint32_t mask = 0;
  if (strcmp(prev.status, next.status) != 0 || prev.saved != next.saved) {
    mask |= UiRegionHeader;
    if (next.screen == UiPaintScreen::Home && prev.saved != next.saved) {
      mask |= UiRegionWifiActions;
    }
  }
  if (prev.store != next.store || strcmp(prev.path, next.path) != 0) {
    mask |= UiRegionFooter;
  }

  const bool home = next.screen == UiPaintScreen::Home;
  if (home && prev.scan != next.scan) {
    mask |= UiRegionProgress | UiRegionControls;
    if (!next.station) {
      mask |= UiRegionNetwork;
    }
  }
  if (home && (prev.svcDone != next.svcDone || prev.svcPlan != next.svcPlan)) {
    mask |= UiRegionProgress;
  }
  if (home && (prev.processed != next.processed || prev.candidates != next.candidates || prev.elapsedSec != next.elapsedSec)) {
    mask |= UiRegionProgress;
    if (!next.station) {
      mask |= UiRegionNetwork;
    }
  }
  if (prev.observed != next.observed || strcmp(prev.newest, next.newest) != 0 ||
      strcmp(prev.newestDetail, next.newestDetail) != 0) {
    if (home) {
      mask |= UiRegionLatest;
    }
    if (next.screen == UiPaintScreen::Hosts) {
      mask |= UiRegionHeader;
    }
  }

  if (prev.shownId != next.shownId) {
    if (!home) {
      return mask == 0 ? UiRegionInPlace : UiRegionAll;
    }
    if (prev.shownH > 0) {
      mask |= regionsTouched(prev.shownY, prev.shownH);
    }
    if (next.shownH > 0) {
      mask |= regionsTouched(next.shownY, next.shownH);
    }
    if (prev.shownH <= 0 && next.shownH <= 0) {
      mask |= UiRegionAll;
    }
  }
  return mask;
}
