#pragma once

#include <stdint.h>

#include "BoardConfig.h"

// Portrait regions for the 222x480 panel. They tile the screen without gaps.
// A progress tick dirties Progress only. A press acknowledgement dirties the
// band that contains that control. A screen change dirties every band once.
enum UiRegion : uint32_t {
  UiRegionNone = 0,
  UiRegionHeader = 1u << 0,
  UiRegionWifiActions = 1u << 1,
  UiRegionNetwork = 1u << 2,
  UiRegionProgress = 1u << 3,
  UiRegionLatest = 1u << 4,
  UiRegionControls = 1u << 5,
  UiRegionFooter = 1u << 6,
  UiRegionAll = 0x7fu,
  // Non-home press acknowledgement. Repaint the control in place.
  UiRegionInPlace = 1u << 7
};

// One reusable PSRAM sprite. Region height stays at or below this.
static constexpr int kUiSpriteW = kPanelWidth;
static constexpr int kUiSpriteH = 160;

enum class UiPaintScreen : uint8_t { Home, Results, Password, Hosts, Settings };

struct UiRegionRect {
  int x;
  int y;
  int w;
  int h;
};

struct UiPaintFrame {
  UiPaintScreen screen = UiPaintScreen::Home;
  int scan = 0;
  bool saved = false;
  bool station = false;
  int shownId = -1;
  int shownY = 0;
  int shownH = 0;
  uint16_t processed = 0;
  uint16_t candidates = 0;
  uint16_t observed = 0;
  uint32_t elapsedSec = 0;
  int page = 0;
  int keyboardPage = 0;
  bool shift = false;
  int passLen = 0;
  int store = 0;
  int profile = 1;
  uint32_t listStamp = 0;
  char path[64] = {};
  char status[48] = {};
  char newest[16] = {};
  char newestDetail[24] = {};
};

// Address progress, 0 when there are no candidates. This is not the observed-device count.
int progressPercent(uint16_t processed, uint16_t candidates);

UiRegionRect uiRegionRect(uint32_t bit);
uint32_t dirtyRegions(const UiPaintFrame& prev, const UiPaintFrame& next);

// Places one text line inside a host card so the whole glyph box stays in the card
// and inside a single dirty region. lineIndex 0 is the top line.
bool uiHostTextY(int cardY, int cardH, int lineIndex, int textH, int* outY);
