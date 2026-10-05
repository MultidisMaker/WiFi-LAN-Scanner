#include <unity.h>

#include "NetMath.h"
#include "PasswordBuffer.h"
#include "ScanClock.h"
#include "ScannerController.h"
#include "UiModel.h"
#include "UiPress.h"

static uint32_t gScanNow = 0;

uint32_t scanNow() { return gScanNow; }

static UiSnapshot homeSnapshot() {
  UiSnapshot snapshot;
  snapshot.phase = UiPhase::Home;
  snapshot.scan = ScanState::Idle;
  return snapshot;
}

void test_press_down_hold_release_once(void) { TEST_ASSERT_TRUE(pressTrackerSelfTest()); }

void test_press_short_ack_and_drag_sequences(void) {
  PressTracker tap;
  const PressStep down = tap.update(true, 7, 1000);
  TEST_ASSERT_TRUE(down.began && !down.fire && tap.shownId() == 7);
  const PressStep held = tap.update(true, 7, 1400);
  TEST_ASSERT_TRUE(!held.fire && tap.shownId() == 7);
  const PressStep up = tap.update(false, 7, 1500);
  TEST_ASSERT_TRUE(up.fire && up.fireId == 7 && tap.shownId() == -1);

  PressTracker drag;
  drag.update(true, 3, 0);
  const PressStep left = drag.update(true, -1, 30);
  const PressStep dragUp = drag.update(false, 3, 40);
  TEST_ASSERT_TRUE(left.cancelled && !dragUp.fire);

  PressTracker slide;
  slide.update(true, 4, 0);
  slide.update(true, -1, 10);
  slide.update(true, 4, 20);
  const PressStep slideUp = slide.update(false, 4, 30);
  TEST_ASSERT_TRUE(!slideUp.fire);

  PressTracker repeat;
  repeat.update(true, 1, 0);
  const PressStep first = repeat.update(false, 1, 200);
  repeat.update(true, 1, 250);
  const PressStep second = repeat.update(false, 1, 400);
  TEST_ASSERT_TRUE(first.fire && second.fire);

  PressTracker miss;
  const PressStep missDown = miss.update(true, -1, 0);
  const PressStep missUp = miss.update(false, -1, 20);
  TEST_ASSERT_TRUE(!missDown.began && !missUp.fire);
}

void test_control_face_four_states(void) {
  TEST_ASSERT_TRUE(controlFace(false, false) == ControlFace::Normal);
  TEST_ASSERT_TRUE(controlFace(false, true) == ControlFace::Pressed);
  TEST_ASSERT_TRUE(controlFace(true, false) == ControlFace::Latched);
  TEST_ASSERT_TRUE(controlFace(true, true) == ControlFace::LatchedPressed);
}

void test_key_glyph_alphabet_and_symbols(void) { TEST_ASSERT_TRUE(keyGlyphSelfTest()); }

void test_mask_password_asterisks_only(void) { TEST_ASSERT_TRUE(maskPasswordSelfTest()); }

void test_password_preserved_across_shift(void) {
  PasswordBuffer buffer;
  TEST_ASSERT_TRUE(buffer.preservedAcrossShift());
  TEST_ASSERT_TRUE(buffer.length() == 0);
  TEST_ASSERT_FALSE(buffer.shiftOn());
}

void test_range_slash24(void) {
  const NetFacts facts = deriveNetFacts(ipv4(192, 168, 0, 20), ipv4(255, 255, 255, 0), ipv4(192, 168, 0, 1),
                                        ipv4(192, 168, 0, 1), ipv4(0, 0, 0, 0));
  TEST_ASSERT_TRUE(facts.valid);
  TEST_ASSERT_TRUE(facts.prefix == 24);
  TEST_ASSERT_TRUE(facts.usableHosts == 254);
  TEST_ASSERT_TRUE(facts.futureScanCount == 254);
  TEST_ASSERT_TRUE(ipv4Equal(facts.network, ipv4(192, 168, 0, 0)));
  TEST_ASSERT_FALSE(facts.hasSecondaryDns);
}

void test_range_slash28_secondary_dns(void) {
  const NetFacts facts = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(1, 1, 1, 1), ipv4(8, 8, 8, 8));
  TEST_ASSERT_TRUE(facts.valid);
  TEST_ASSERT_TRUE(facts.prefix == 28);
  TEST_ASSERT_TRUE(facts.usableHosts == 14);
  TEST_ASSERT_TRUE(facts.futureScanCount == 14);
  TEST_ASSERT_TRUE(facts.hasSecondaryDns);
  TEST_ASSERT_TRUE(ipv4Equal(facts.network, ipv4(10, 0, 0, 0)));
}

void test_range_slash16_capped(void) {
  const NetFacts facts = deriveNetFacts(ipv4(10, 1, 0, 9), ipv4(255, 255, 0, 0), ipv4(10, 1, 0, 1),
                                        ipv4(10, 1, 0, 1), ipv4(0, 0, 0, 0));
  TEST_ASSERT_TRUE(facts.valid);
  TEST_ASSERT_TRUE(facts.prefix == 16);
  TEST_ASSERT_TRUE(facts.usableHosts == 65534);
  TEST_ASSERT_TRUE(facts.futureScanCount == 256);
  TEST_ASSERT_TRUE(facts.futureScanCap == 256);
}

void test_range_invalid_masks(void) {
  const NetFacts broken = deriveNetFacts(ipv4(10, 0, 0, 1), ipv4(255, 0, 255, 0), ipv4(10, 0, 0, 1),
                                         ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  const NetFacts empty = deriveNetFacts(ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0), ipv4(10, 0, 0, 1), ipv4(10, 0, 0, 1),
                                        ipv4(0, 0, 0, 0));
  const NetFacts host = deriveNetFacts(ipv4(10, 0, 0, 1), ipv4(255, 255, 255, 255), ipv4(10, 0, 0, 1),
                                       ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  TEST_ASSERT_FALSE(broken.valid);
  TEST_ASSERT_FALSE(empty.valid);
  TEST_ASSERT_FALSE(host.valid);
  TEST_ASSERT_TRUE(broken.futureScanCount == 0);
}

void test_scanner_timer_and_invalid_transitions(void) {
  gScanNow = 1000;
  ScannerController scanner;
  scanner.pause();
  scanner.resume();
  scanner.stop();
  scanner.acknowledge();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle);
  scanner.start();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Starting);
  scanner.pause();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Starting);
  gScanNow = 1199;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Starting);
  gScanNow = 1200;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Scanning);
  scanner.start();
  scanner.resume();
  scanner.acknowledge();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Scanning);
  scanner.pause();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Paused);
  scanner.start();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Paused);
  scanner.resume();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Scanning);
  scanner.stop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Stopping);
  gScanNow = 1399;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Stopping);
  gScanNow = 1400;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Complete);
  scanner.stop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Complete);
  scanner.acknowledge();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle);
}

void test_scanner_self_test(void) {
  gScanNow = 5000;
  ScannerController scanner;
  TEST_ASSERT_TRUE(scanner.selfTest());
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle);
}

void test_ui_hit_find_edges_and_miss(void) {
  const UiSnapshot snapshot = homeSnapshot();
  UiControl controls[40];
  const int count = collectUiControls(controls, 40, snapshot);
  TEST_ASSERT_TRUE(hitUiControl(controls, count, 20, 80) == IdFind);
  TEST_ASSERT_TRUE(hitUiControl(controls, count, 213, 80) == IdFind);
  TEST_ASSERT_TRUE(hitUiControl(controls, count, 214, 80) == -1);
  TEST_ASSERT_TRUE(hitUiControl(controls, count, 0, 0) == -1);
}

void test_ui_tap_ack_and_drag_off(void) {
  const UiSnapshot snapshot = homeSnapshot();
  const UiGesture shown = playTap(snapshot, 20, 80, 40, 80);
  TEST_ASSERT_TRUE(shown.hitId == IdFind && shown.fire && !shown.cancelled);
  TEST_ASSERT_TRUE(shown.shownAtSample == IdFind);
  TEST_ASSERT_TRUE(shown.faceWhileDown == ControlFace::Pressed);
  const UiGesture cleared = playTap(snapshot, 20, 80, 40, 120);
  TEST_ASSERT_TRUE(cleared.fire && cleared.shownAtSample == -1);
  const UiGesture drag = playDrag(snapshot, 20, 80, 0, 0, 30, 40);
  TEST_ASSERT_TRUE(drag.cancelled && !drag.fire);
}

void test_ui_shift_latch_and_alphabet(void) {
  UiSnapshot lower;
  lower.phase = UiPhase::Password;
  TEST_ASSERT_TRUE(alphabetCaseIs(lower, false));
  TEST_ASSERT_FALSE(alphabetCaseIs(lower, true));
  UiSnapshot upper = lower;
  upper.shift = true;
  TEST_ASSERT_TRUE(alphabetCaseIs(upper, true));
  TEST_ASSERT_FALSE(alphabetCaseIs(upper, false));
  UiControl controls[40];
  const int count = collectUiControls(controls, 40, upper);
  bool latched = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdShift) {
      latched = controls[i].latched;
    }
  }
  TEST_ASSERT_TRUE(latched);
  const UiGesture pressed = playTap(upper, 10, 440, 40, 80);
  TEST_ASSERT_TRUE(pressed.hitId == IdShift && pressed.fire);
  TEST_ASSERT_TRUE(pressed.faceWhileDown == ControlFace::LatchedPressed);
}

void test_ui_results_row_uses_role_name(void) {
  UiSnapshot snapshot;
  snapshot.phase = UiPhase::Results;
  snapshot.rowPresent[0] = true;
  const UiGesture gesture = playTap(snapshot, 20, 50, 40, 80);
  TEST_ASSERT_TRUE(gesture.hitId == IdRow0 && gesture.fire);
  TEST_ASSERT_EQUAL_STRING("row", uiControlName(gesture.hitId));
}

static int gFailures = 0;

void setup() {
  UNITY_BEGIN();
  RUN_TEST(test_press_down_hold_release_once);
  RUN_TEST(test_press_short_ack_and_drag_sequences);
  RUN_TEST(test_control_face_four_states);
  RUN_TEST(test_key_glyph_alphabet_and_symbols);
  RUN_TEST(test_mask_password_asterisks_only);
  RUN_TEST(test_password_preserved_across_shift);
  RUN_TEST(test_range_slash24);
  RUN_TEST(test_range_slash28_secondary_dns);
  RUN_TEST(test_range_slash16_capped);
  RUN_TEST(test_range_invalid_masks);
  RUN_TEST(test_scanner_timer_and_invalid_transitions);
  RUN_TEST(test_scanner_self_test);
  RUN_TEST(test_ui_hit_find_edges_and_miss);
  RUN_TEST(test_ui_tap_ack_and_drag_off);
  RUN_TEST(test_ui_shift_latch_and_alphabet);
  RUN_TEST(test_ui_results_row_uses_role_name);
  gFailures = UNITY_END();
}

void loop() {}

int main() {
  setup();
  return gFailures;
}
