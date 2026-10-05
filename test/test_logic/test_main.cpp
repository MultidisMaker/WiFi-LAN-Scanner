#include <unity.h>

#include "BoardConfig.h"
#include "CandidatePlan.h"
#include "FakeDiscovery.h"
#include "HostInventory.h"
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

static bool planHas(const CandidatePlan& plan, const Ipv4& ip) {
  for (uint16_t i = 0; i < plan.count; ++i) {
    if (ipv4Equal(plan.address[i], ip)) {
      return true;
    }
  }
  return false;
}

static NetFacts lan24() {
  return deriveNetFacts(ipv4(192, 168, 0, 20), ipv4(255, 255, 255, 0), ipv4(192, 168, 0, 1), ipv4(192, 168, 0, 1),
                        ipv4(0, 0, 0, 0));
}

static void armReady(ScannerController& scanner, FakeDiscoveryBackend& backend, const NetFacts& facts, uint32_t waitMs) {
  backend.clearScripts();
  backend.setWaitMs(waitMs);
  scanner.setBackend(&backend);
  scanner.armConnectedFacts(facts);
}

void test_scanner_timer_and_invalid_transitions(void) {
  gScanNow = 1000;
  ScannerController scanner;
  FakeDiscoveryBackend backend;
  armReady(scanner, backend, lan24(), 60000);
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

void test_candidates_slash24_and_slash28(void) {
  const CandidatePlan slash24 = buildCandidatePlan(lan24());
  TEST_ASSERT_TRUE(slash24.valid);
  TEST_ASSERT_TRUE(slash24.count == 253);
  TEST_ASSERT_FALSE(slash24.capped);
  TEST_ASSERT_TRUE(slash24.gatewayIncluded);
  TEST_ASSERT_FALSE(slash24.gatewayForced);
  TEST_ASSERT_TRUE(ipv4Equal(slash24.address[0], ipv4(192, 168, 0, 1)));
  TEST_ASSERT_TRUE(ipv4Equal(slash24.address[1], ipv4(192, 168, 0, 2)));
  TEST_ASSERT_TRUE(ipv4Equal(slash24.address[slash24.count - 1], ipv4(192, 168, 0, 254)));
  TEST_ASSERT_FALSE(planHas(slash24, ipv4(192, 168, 0, 0)));
  TEST_ASSERT_FALSE(planHas(slash24, ipv4(192, 168, 0, 20)));
  TEST_ASSERT_FALSE(planHas(slash24, ipv4(192, 168, 0, 255)));

  const CandidatePlan slash28 =
      buildCandidatePlan(deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0)));
  TEST_ASSERT_TRUE(slash28.valid && slash28.count == 13 && !slash28.capped);
  TEST_ASSERT_TRUE(planHas(slash28, ipv4(10, 0, 0, 1)));
  TEST_ASSERT_TRUE(planHas(slash28, ipv4(10, 0, 0, 14)));
  TEST_ASSERT_FALSE(planHas(slash28, ipv4(10, 0, 0, 5)));
  TEST_ASSERT_FALSE(planHas(slash28, ipv4(10, 0, 0, 0)));
  TEST_ASSERT_FALSE(planHas(slash28, ipv4(10, 0, 0, 15)));
}

void test_candidates_large_subnet_keeps_gateway(void) {
  const CandidatePlan plan =
      buildCandidatePlan(deriveNetFacts(ipv4(10, 1, 0, 9), ipv4(255, 255, 0, 0), ipv4(10, 1, 5, 5), ipv4(10, 1, 0, 1),
                                        ipv4(0, 0, 0, 0)));
  TEST_ASSERT_TRUE(plan.valid && plan.capped && plan.count == 256 && plan.gatewayForced && plan.gatewayIncluded);
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[0], ipv4(10, 1, 0, 1)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[7], ipv4(10, 1, 0, 8)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[8], ipv4(10, 1, 0, 10)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[255], ipv4(10, 1, 5, 5)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 0, 9)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 0, 0)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 1, 1)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 255, 255)));
}

void test_candidates_reject_invalid_range(void) {
  const CandidatePlan broken =
      buildCandidatePlan(deriveNetFacts(ipv4(10, 0, 0, 1), ipv4(255, 0, 255, 0), ipv4(10, 0, 0, 1), ipv4(10, 0, 0, 1),
                                        ipv4(0, 0, 0, 0)));
  TEST_ASSERT_FALSE(broken.valid);
  TEST_ASSERT_TRUE(broken.count == 0);
}

void test_scanner_refuses_disconnected_and_invalid(void) {
  gScanNow = 2000;
  ScannerController scanner;
  FakeDiscoveryBackend backend;
  backend.setWaitMs(0);
  scanner.setBackend(&backend);
  scanner.armDisconnected();
  scanner.start();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle);
  scanner.armConnectedFacts(deriveNetFacts(ipv4(10, 0, 0, 1), ipv4(255, 0, 255, 0), ipv4(10, 0, 0, 1),
                                           ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0)));
  scanner.start();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle);
  scanner.setBackend(nullptr);
  scanner.armConnectedFacts(lan24());
  scanner.start();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle);
}

static void reachScanning(ScannerController& scanner) {
  scanner.start();
  gScanNow += kScannerTransitionMs;
  scanner.loop();
}

void test_discovery_observes_dedupes_and_skips_silence(void) {
  gScanNow = 3000;
  ScannerController scanner;
  FakeDiscoveryBackend backend;
  const NetFacts facts = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  armReady(scanner, backend, facts, 0);
  const uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  backend.addObservation(ipv4(10, 0, 0, 1), true, mac, true, 7);
  backend.addObservation(ipv4(10, 0, 0, 2), false, nullptr, false, 0);
  reachScanning(scanner);
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Scanning);
  int guard = 0;
  while (scanner.state() != ScanState::Complete && guard < 48) {
    scanner.loop();
    ++guard;
  }
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Complete);
  TEST_ASSERT_TRUE(scanner.processedCount() == 13);
  TEST_ASSERT_TRUE(scanner.observedCount() == 2);
  const ObservedHost* gateway = scanner.hostAt(0);
  const ObservedHost* quiet = scanner.hostAt(1);
  TEST_ASSERT_TRUE(gateway != nullptr && gateway->hasMac && ipv4Equal(gateway->ip, ipv4(10, 0, 0, 1)));
  TEST_ASSERT_TRUE(gateway->evidence == EvidenceRank::Neighbor);
  TEST_ASSERT_TRUE(quiet != nullptr && !quiet->hasMac && ipv4Equal(quiet->ip, ipv4(10, 0, 0, 2)));
  TEST_ASSERT_TRUE(quiet->evidence == EvidenceRank::Answered);
  TEST_ASSERT_TRUE(scanner.newest() == quiet);

  HostInventory inventory;
  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, mac, true, 7, 10, "fake");
  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Answered, false, nullptr, false, 0, 30, "fake");
  TEST_ASSERT_TRUE(inventory.count() == 1);
  TEST_ASSERT_TRUE(inventory.at(0)->hasMac && inventory.at(0)->lastSeenMs == 30 && inventory.at(0)->firstSeenMs == 10);
  const uint8_t same[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  inventory.observe(ipv4(10, 0, 0, 9), EvidenceRank::Neighbor, true, same, true, 4, 40, "fake");
  TEST_ASSERT_TRUE(inventory.count() == 1);
  TEST_ASSERT_TRUE(ipv4Equal(inventory.at(0)->ip, ipv4(10, 0, 0, 9)));
  TEST_ASSERT_TRUE(inventory.at(0)->firstSeenMs == 10);
}

void test_pause_resume_stop_reset_and_repeat(void) {
  gScanNow = 8000;
  ScannerController scanner;
  FakeDiscoveryBackend backend;
  const NetFacts facts = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  armReady(scanner, backend, facts, 50);
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
  backend.addObservation(ipv4(10, 0, 0, 1), true, mac, true, 5);
  reachScanning(scanner);
  gScanNow += 40;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.processedCount() == 0 && scanner.state() == ScanState::Scanning);
  scanner.pause();
  gScanNow += 500;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Paused && scanner.processedCount() == 0);
  scanner.resume();
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Scanning && scanner.processedCount() == 1 && scanner.observedCount() == 1);
  scanner.stop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Stopping && scanner.observedCount() == 1);
  gScanNow += kScannerTransitionMs - 1;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Stopping && scanner.observedCount() == 1);
  gScanNow += 1;
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Complete && scanner.observedCount() == 1);
  scanner.loop();
  TEST_ASSERT_TRUE(scanner.observedCount() == 1);

  scanner.reset();
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Idle && scanner.observedCount() == 0 && scanner.candidateCount() == 0);
  armReady(scanner, backend, facts, 0);
  backend.addObservation(ipv4(10, 0, 0, 1), true, mac, true, 5);
  reachScanning(scanner);
  int guard = 0;
  while (scanner.state() != ScanState::Complete && guard < 48) {
    scanner.loop();
    ++guard;
  }
  TEST_ASSERT_TRUE(scanner.observedCount() == 1 && scanner.processedCount() == 13);
  const int startsAfterFirst = backend.starts();
  reachScanning(scanner);
  guard = 0;
  while (scanner.state() != ScanState::Complete && guard < 48) {
    scanner.loop();
    ++guard;
  }
  TEST_ASSERT_TRUE(scanner.observedCount() == 1);
  TEST_ASSERT_TRUE(backend.starts() > startsAfterFirst);
}

void test_ui_progress_and_host_rows(void) {
  UiSnapshot home = homeSnapshot();
  home.showDashboard = true;
  const char* progress = "SCANNING 2/13";
  const char* newest = "10.0.0.1";
  const char* unknown = "MAC unknown";
  for (int i = 0; progress[i] != '\0'; ++i) {
    home.progressLabel[i] = progress[i];
  }
  for (int i = 0; newest[i] != '\0'; ++i) {
    home.newestLabel[i] = newest[i];
  }
  for (int i = 0; unknown[i] != '\0'; ++i) {
    home.newestDetail[i] = unknown[i];
  }
  UiControl controls[40];
  const int count = collectUiControls(controls, 40, home);
  bool sawProgress = false;
  bool sawNewest = false;
  bool sawReset = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdProgress) {
      sawProgress = true;
      TEST_ASSERT_EQUAL_STRING("progress", uiControlName(controls[i].id));
      TEST_ASSERT_EQUAL_STRING("SCANNING 2/13", controls[i].label);
    }
    if (controls[i].id == IdNewest) {
      sawNewest = true;
      TEST_ASSERT_EQUAL_STRING("10.0.0.1", controls[i].label);
      TEST_ASSERT_EQUAL_STRING("MAC unknown", controls[i].detail);
    }
    if (controls[i].id == IdReset) {
      sawReset = true;
    }
  }
  TEST_ASSERT_TRUE(sawProgress && sawNewest && sawReset);

  UiSnapshot hosts;
  hosts.phase = UiPhase::Hosts;
  hosts.rowPresent[0] = true;
  const char* ip = "10.0.0.2";
  const char* detail = "MAC unknown";
  for (int i = 0; ip[i] != '\0'; ++i) {
    hosts.rowLabel[0][i] = ip[i];
  }
  for (int i = 0; detail[i] != '\0'; ++i) {
    hosts.rowDetail[0][i] = detail[i];
  }
  const UiGesture gesture = playTap(hosts, 20, 50, 40, 80);
  TEST_ASSERT_TRUE(gesture.hitId == IdRow0 && gesture.fire);
  TEST_ASSERT_EQUAL_STRING("row", uiControlName(gesture.hitId));
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
  RUN_TEST(test_candidates_slash24_and_slash28);
  RUN_TEST(test_candidates_large_subnet_keeps_gateway);
  RUN_TEST(test_candidates_reject_invalid_range);
  RUN_TEST(test_scanner_timer_and_invalid_transitions);
  RUN_TEST(test_scanner_self_test);
  RUN_TEST(test_scanner_refuses_disconnected_and_invalid);
  RUN_TEST(test_discovery_observes_dedupes_and_skips_silence);
  RUN_TEST(test_pause_resume_stop_reset_and_repeat);
  RUN_TEST(test_ui_progress_and_host_rows);
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
