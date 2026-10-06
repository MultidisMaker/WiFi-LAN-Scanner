#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "ActionAck.h"
#include "AppActions.h"
#include "BoardConfig.h"
#include "CandidatePlan.h"
#include "DnsPtr.h"
#include "FakeDiscovery.h"
#include "HostInventory.h"
#include "InventoryExport.h"
#include "InventoryStore.h"
#include "NameRecord.h"
#include "NetMath.h"
#include "Oui.h"
#include "OuiData.h"
#include "PasswordBuffer.h"
#include "RemoteProtocol.h"
#include "ResourceFormat.h"
#include "ScanClock.h"
#include "UiStatus.h"
#include "ScannerController.h"
#include "ServiceProfile.h"
#include "ServiceResultView.h"
#include "ServiceScan.h"
#include "UiModel.h"
#include "UiPress.h"
#include "UiRender.h"

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
  TEST_ASSERT_TRUE(plan.valid && plan.capped && plan.count == 255 && !plan.gatewayForced && plan.gatewayIncluded);
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[0], ipv4(10, 1, 0, 1)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[7], ipv4(10, 1, 0, 8)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[8], ipv4(10, 1, 0, 10)));
  TEST_ASSERT_TRUE(planHas(plan, ipv4(10, 1, 0, 255)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[plan.count - 1], ipv4(10, 1, 5, 5)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 0, 9)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 0, 0)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 1, 0)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 255, 255)));

  const CandidatePlan around =
      buildCandidatePlan(deriveNetFacts(ipv4(10, 1, 104, 163), ipv4(255, 255, 0, 0), ipv4(10, 1, 0, 1),
                                        ipv4(10, 1, 0, 1), ipv4(0, 0, 0, 0)));
  TEST_ASSERT_TRUE(around.valid && around.capped && around.count == 256 && !around.gatewayForced && around.gatewayIncluded);
  TEST_ASSERT_TRUE(ipv4Equal(around.address[0], ipv4(10, 1, 0, 1)));
  TEST_ASSERT_TRUE(planHas(around, ipv4(10, 1, 104, 0)));
  TEST_ASSERT_TRUE(planHas(around, ipv4(10, 1, 104, 255)));
  TEST_ASSERT_FALSE(planHas(around, ipv4(10, 1, 104, 163)));
  TEST_ASSERT_FALSE(planHas(around, ipv4(10, 1, 105, 0)));
  TEST_ASSERT_TRUE(around.count <= kCandidateCap);
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

void test_name_sanitize_and_precedence(void) {
  char out[32];
  TEST_ASSERT_TRUE(sanitizeHostName("printer.local", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("printer", out);
  TEST_ASSERT_TRUE(sanitizeHostName("Camera.LOCAL.", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("Camera", out);
  TEST_ASSERT_FALSE(sanitizeHostName("", out, sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_FALSE(sanitizeHostName("@@@", out, sizeof(out)));
  TEST_ASSERT_FALSE(sanitizeHostName(nullptr, out, sizeof(out)));

  char longName[80];
  for (int i = 0; i < 79; ++i) {
    longName[i] = 'b';
  }
  longName[40] = ' ';
  longName[41] = '/';
  longName[79] = '\0';
  TEST_ASSERT_TRUE(sanitizeHostName(longName, out, sizeof(out)));
  TEST_ASSERT_EQUAL_UINT(31, strlen(out));
  TEST_ASSERT_EQUAL_CHAR('b', out[0]);
  TEST_ASSERT_EQUAL_CHAR('b', out[30]);

  TEST_ASSERT_TRUE(preferIncomingName(NameSource::None, "", NameSource::ReverseDns, "dns-name"));
  TEST_ASSERT_TRUE(preferIncomingName(NameSource::Mdns, "printer", NameSource::ReverseDns, "aaa"));
  TEST_ASSERT_FALSE(preferIncomingName(NameSource::ReverseDns, "dns-name", NameSource::Mdns, "zzz"));
  TEST_ASSERT_TRUE(preferIncomingName(NameSource::Mdns, "printer", NameSource::Mdns, "alpha"));
  TEST_ASSERT_FALSE(preferIncomingName(NameSource::Mdns, "alpha", NameSource::Mdns, "printer"));
  TEST_ASSERT_FALSE(preferIncomingName(NameSource::Mdns, "alpha", NameSource::Mdns, "alpha"));
  TEST_ASSERT_FALSE(preferIncomingName(NameSource::Mdns, "alpha", NameSource::Mdns, ""));
}

void test_inventory_name_preserves_host_and_allows_duplicates(void) {
  HostInventory inventory;
  const uint8_t macA[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x11};
  const uint8_t macB[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x12};
  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, macA, true, 5, 10, "fake");
  inventory.observe(ipv4(10, 0, 0, 2), EvidenceRank::Neighbor, true, macB, true, 6, 11, "fake");
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 9), "ghost", NameSource::Mdns) == NameApply::MissingHost);

  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "printer.local", NameSource::Mdns) == NameApply::Applied);
  const ObservedHost* first = inventory.at(0);
  TEST_ASSERT_TRUE(first != nullptr && strcmp(first->name, "printer") == 0 && first->nameSource == NameSource::Mdns);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "@@@", NameSource::Mdns) == NameApply::Rejected);
  TEST_ASSERT_EQUAL_STRING("printer", inventory.at(0)->name);
  TEST_ASSERT_TRUE(inventory.at(0)->hasMac && inventory.at(0)->mac[5] == 0x11);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "aaa-lower", NameSource::ReverseDns) == NameApply::Applied);
  TEST_ASSERT_EQUAL_STRING("aaa-lower", inventory.at(0)->name);
  TEST_ASSERT_TRUE(inventory.at(0)->nameSource == NameSource::ReverseDns);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "zzz-mdns", NameSource::Mdns) == NameApply::Kept);
  TEST_ASSERT_EQUAL_STRING("aaa-lower", inventory.at(0)->name);

  char longName[48];
  for (int i = 0; i < 47; ++i) {
    longName[i] = 'c';
  }
  longName[47] = '\0';
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 2), "", NameSource::Mdns) == NameApply::Rejected);
  TEST_ASSERT_TRUE(inventory.at(1)->name[0] == '\0' && inventory.at(1)->hasMac && inventory.at(1)->mac[5] == 0x12);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 2), longName, NameSource::Mdns) == NameApply::Applied);
  TEST_ASSERT_EQUAL_UINT(31, strlen(inventory.at(1)->name));

  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "alpha", NameSource::Mdns) == NameApply::Kept);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 2), "alpha", NameSource::Mdns) == NameApply::Applied);
  TEST_ASSERT_TRUE(inventory.count() == 2);
  TEST_ASSERT_EQUAL_STRING("aaa-lower", inventory.at(0)->name);
  TEST_ASSERT_EQUAL_STRING("alpha", inventory.at(1)->name);
  TEST_ASSERT_TRUE(ipv4Equal(inventory.at(0)->ip, ipv4(10, 0, 0, 1)));
  TEST_ASSERT_TRUE(ipv4Equal(inventory.at(1)->ip, ipv4(10, 0, 0, 2)));

  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, macA, true, 9, 30, "fake");
  TEST_ASSERT_TRUE(inventory.count() == 2);
  TEST_ASSERT_EQUAL_STRING("aaa-lower", inventory.at(0)->name);
  TEST_ASSERT_EQUAL_UINT(30, inventory.at(0)->lastSeenMs);

  gScanNow = 8000;
  ScannerController scanner;
  FakeDiscoveryBackend backend;
  const NetFacts facts = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  armReady(scanner, backend, facts, 0);
  backend.addObservation(ipv4(10, 0, 0, 1), true, macA, true, 4);
  reachScanning(scanner);
  int guard = 0;
  while (scanner.state() != ScanState::Complete && guard < 48) {
    scanner.loop();
    ++guard;
  }
  TEST_ASSERT_TRUE(scanner.rememberName(ipv4(10, 0, 0, 1), "scanner-host.local", NameSource::Mdns) == NameApply::Applied);
  const ObservedHost* scanned = scanner.hostAt(0);
  TEST_ASSERT_TRUE(scanned != nullptr && strcmp(scanned->name, "scanner-host") == 0);
  TEST_ASSERT_TRUE(scanned->hasMac && scanned->mac[5] == 0x11);
  TEST_ASSERT_TRUE(scanner.rememberName(ipv4(10, 0, 0, 3), "missing", NameSource::Mdns) == NameApply::MissingHost);
}

void test_ui_host_detail_with_and_without_name(void) {
  const uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  char unnamed[40];
  char named[40];
  formatHostDetail(unnamed, sizeof(unnamed), NameSource::None, "", false, nullptr);
  TEST_ASSERT_EQUAL_STRING("Name: unknown MAC unknown", unnamed);
  formatHostDetail(named, sizeof(named), NameSource::Mdns, "alpha", true, mac);
  TEST_ASSERT_EQUAL_STRING("Name: alpha 02:11:22:33:44:55", named);
  char dnsDetail[40];
  formatHostDetail(dnsDetail, sizeof(dnsDetail), NameSource::ReverseDns, "ns-host", true, mac);
  TEST_ASSERT_EQUAL_STRING("Name: ns-host 02:11:22:33:44:55", dnsDetail);

  char stored[32];
  for (int i = 0; i < 31; ++i) {
    stored[i] = 'n';
  }
  stored[31] = '\0';
  char clipped[40];
  formatHostDetail(clipped, sizeof(clipped), NameSource::Mdns, stored, false, nullptr);
  TEST_ASSERT_EQUAL_STRING("Name: nnnnnnnnnnnnnnn MAC unknown", clipped);
  TEST_ASSERT_TRUE(strlen(clipped) <= 33);

  UiControl controls[8];
  UiSnapshot namedRow;
  namedRow.phase = UiPhase::Hosts;
  namedRow.rowPresent[0] = true;
  const char* ip = "10.0.0.1";
  for (int i = 0; ip[i] != '\0'; ++i) {
    namedRow.rowLabel[0][i] = ip[i];
  }
  for (int i = 0; named[i] != '\0'; ++i) {
    namedRow.rowDetail[0][i] = named[i];
  }
  int count = collectUiControls(controls, 8, namedRow);
  bool sawNamed = false;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.0.0.1") == 0) {
      sawNamed = true;
      TEST_ASSERT_EQUAL_STRING("Name: alpha 02:11:22:33:44:55", controls[i].detail);
    }
  }
  UiSnapshot unknownRow;
  unknownRow.phase = UiPhase::Hosts;
  unknownRow.rowPresent[0] = true;
  const char* secondIp = "10.0.0.2";
  for (int i = 0; secondIp[i] != '\0'; ++i) {
    unknownRow.rowLabel[0][i] = secondIp[i];
  }
  for (int i = 0; unnamed[i] != '\0'; ++i) {
    unknownRow.rowDetail[0][i] = unnamed[i];
  }
  count = collectUiControls(controls, 8, unknownRow);
  bool sawUnknown = false;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.0.0.2") == 0) {
      sawUnknown = true;
      TEST_ASSERT_EQUAL_STRING("Name: unknown MAC unknown", controls[i].detail);
    }
  }
  TEST_ASSERT_TRUE(sawNamed && sawUnknown);
}

void test_oui_parse_classify_and_lookup(void) {
  uint32_t prefix = 0;
  TEST_ASSERT_TRUE(parseOuiAssignment("AABBCC", &prefix));
  TEST_ASSERT_EQUAL_UINT32(0x00AABBCCu, prefix);
  TEST_ASSERT_TRUE(parseOuiAssignment("aa:bb:cc", &prefix));
  TEST_ASSERT_EQUAL_UINT32(0x00AABBCCu, prefix);
  TEST_ASSERT_TRUE(parseOuiAssignment("AA-BB-CC", &prefix));
  TEST_ASSERT_EQUAL_UINT32(0x00AABBCCu, prefix);
  TEST_ASSERT_TRUE(parseOuiAssignment("  aabbcc  ", &prefix));
  TEST_ASSERT_EQUAL_UINT32(0x00AABBCCu, prefix);
  TEST_ASSERT_FALSE(parseOuiAssignment(nullptr, &prefix));
  TEST_ASSERT_FALSE(parseOuiAssignment("", &prefix));
  TEST_ASSERT_FALSE(parseOuiAssignment("GG0000", &prefix));
  TEST_ASSERT_FALSE(parseOuiAssignment("AA:BB:CC:DD", &prefix));
  TEST_ASSERT_FALSE(parseOuiAssignment("AA:BB-CC", &prefix));
  TEST_ASSERT_FALSE(parseOuiAssignment("AABBCCD", &prefix));
  TEST_ASSERT_FALSE(parseOuiAssignment("AA:BB:CC extra", &prefix));

  char cleaned[80];
  TEST_ASSERT_TRUE(sanitizeManufacturer("  Jetway Information Co., Ltd.  ", cleaned, sizeof(cleaned)));
  TEST_ASSERT_EQUAL_STRING("Jetway Information Co., Ltd", cleaned);
  TEST_ASSERT_FALSE(sanitizeManufacturer("@@@", cleaned, sizeof(cleaned)));
  TEST_ASSERT_FALSE(sanitizeManufacturer(nullptr, cleaned, sizeof(cleaned)));
  char longOrg[160];
  for (int i = 0; i < 159; ++i) {
    longOrg[i] = 'M';
  }
  longOrg[159] = '\0';
  TEST_ASSERT_TRUE(sanitizeManufacturer(longOrg, cleaned, sizeof(cleaned)));
  TEST_ASSERT_EQUAL_UINT(64, strlen(cleaned));

  TEST_ASSERT_EQUAL_STRING("CERN", preferredOuiName("NETWORK RESEARCH CORPORATION", "CERN"));
  TEST_ASSERT_EQUAL_STRING("Acme", preferredOuiName("Acme", ""));
  TEST_ASSERT_EQUAL_STRING("Alpha", preferredOuiName("Alpha", "Zebra"));
  TEST_ASSERT_TRUE(preferredOuiName("Acme", nullptr) != nullptr && strcmp(preferredOuiName("Acme", nullptr), "Acme") == 0);

  const uint8_t globalMac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t localMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t groupMac[6] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t mixedMac[6] = {0x03, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  TEST_ASSERT_TRUE(classifyMac(globalMac) == MacClass::Global);
  TEST_ASSERT_TRUE(classifyMac(localMac) == MacClass::Local);
  TEST_ASSERT_TRUE(classifyMac(groupMac) == MacClass::Group);
  TEST_ASSERT_TRUE(classifyMac(mixedMac) == MacClass::Group);
  TEST_ASSERT_TRUE(classifyMac(broadcast) == MacClass::Group);
  TEST_ASSERT_TRUE(classifyMac(nullptr) == MacClass::Absent);

  static const char kNames[] = "Acme Widgets\0Tail Vendor\0Hidden Vendor";
  static const OuiEntry kEntries[] = {
      {0x001122u, 0u},
      {0x00ABCDu, 13u},
      {0x021122u, 25u},
  };
  TEST_ASSERT_EQUAL_STRING("Tail Vendor", kNames + 13);
  TEST_ASSERT_EQUAL_STRING("Hidden Vendor", kNames + 25);
  const OuiTable table = {kEntries, 3u, kNames};
  const OuiResult known = lookupOui(table, globalMac);
  TEST_ASSERT_TRUE(known.state == OuiState::Known && known.name != nullptr);
  TEST_ASSERT_EQUAL_STRING("Acme Widgets", known.name);
  const uint8_t tailMac[6] = {0x00, 0xAB, 0xCD, 0x01, 0x02, 0x03};
  const OuiResult tail = lookupOui(table, tailMac);
  TEST_ASSERT_TRUE(tail.state == OuiState::Known && tail.name != nullptr);
  TEST_ASSERT_EQUAL_STRING("Tail Vendor", tail.name);
  const uint8_t unknownMac[6] = {0x00, 0x44, 0x55, 0x66, 0x77, 0x88};
  const OuiResult unknown = lookupOui(table, unknownMac);
  TEST_ASSERT_TRUE(unknown.state == OuiState::Unknown && unknown.name == nullptr && unknown.macClass == MacClass::Global);
  const OuiResult local = lookupOui(table, localMac);
  TEST_ASSERT_TRUE(local.state == OuiState::Local && local.name == nullptr);
  const OuiResult group = lookupOui(table, groupMac);
  TEST_ASSERT_TRUE(group.state == OuiState::Group && group.name == nullptr);
  const OuiResult missing = lookupOui(OuiTable{}, globalMac);
  TEST_ASSERT_TRUE(missing.state == OuiState::DataMissing && missing.name == nullptr);
  const OuiResult absent = lookupOui(table, nullptr);
  TEST_ASSERT_TRUE(absent.state == OuiState::None && absent.macClass == MacClass::Absent);
  const OuiTable embedded = embeddedOuiTable();
  TEST_ASSERT_TRUE(embedded.entries == nullptr && embedded.count == 0);
}

void test_inventory_oui_preserves_host_without_table(void) {
  HostInventory inventory;
  const uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, mac, true, 5, 10, "fake");
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "printer.local", NameSource::Mdns) == NameApply::Applied);
  inventory.enrichManufacturer(0, OuiTable{});
  const ObservedHost* host = inventory.at(0);
  TEST_ASSERT_TRUE(host != nullptr);
  TEST_ASSERT_TRUE(host->ouiState == OuiState::DataMissing && host->manufacturer == nullptr);
  TEST_ASSERT_EQUAL_STRING("printer", host->name);
  TEST_ASSERT_TRUE(host->nameSource == NameSource::Mdns);
  TEST_ASSERT_TRUE(host->hasMac && host->mac[0] == 0x00 && host->mac[2] == 0x22);
  TEST_ASSERT_TRUE(ipv4Equal(host->ip, ipv4(10, 0, 0, 1)));
  TEST_ASSERT_EQUAL_STRING("fake", host->method);

  static const char kNames[] = "Acme Widgets";
  static const OuiEntry kEntries[] = {{0x001122u, 0u}};
  const OuiTable table = {kEntries, 1u, kNames};
  inventory.enrichManufacturer(0, table);
  host = inventory.at(0);
  TEST_ASSERT_TRUE(host->ouiState == OuiState::Known && host->manufacturer != nullptr);
  TEST_ASSERT_EQUAL_STRING("Acme Widgets", host->manufacturer);
  TEST_ASSERT_EQUAL_STRING("printer", host->name);
  TEST_ASSERT_TRUE(ipv4Equal(host->ip, ipv4(10, 0, 0, 1)));

  const uint8_t other[6] = {0x00, 0x44, 0x55, 0x66, 0x77, 0x88};
  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, other, true, 6, 20, "fake");
  host = inventory.at(0);
  TEST_ASSERT_TRUE(host->ouiState == OuiState::Unset && host->manufacturer == nullptr);
  TEST_ASSERT_EQUAL_STRING("printer", host->name);
  TEST_ASSERT_TRUE(host->hasMac && host->mac[1] == 0x44);
  inventory.enrichManufacturer(0, table);
  host = inventory.at(0);
  TEST_ASSERT_TRUE(host->ouiState == OuiState::Unknown && host->manufacturer == nullptr);
  TEST_ASSERT_EQUAL_STRING("printer", host->name);
  TEST_ASSERT_EQUAL_UINT(20, host->lastSeenMs);
}

void test_ui_manufacturer_states(void) {
  char line[32];
  formatOuiLine(line, sizeof(line), OuiState::Known, "Acme Widgets");
  TEST_ASSERT_EQUAL_STRING("Acme Widgets", line);
  char longName[80];
  for (int i = 0; i < 40; ++i) {
    longName[i] = 'M';
  }
  longName[40] = '\0';
  formatOuiLine(line, sizeof(line), OuiState::Known, longName);
  TEST_ASSERT_EQUAL_UINT(31, strlen(line));
  formatOuiLine(line, sizeof(line), OuiState::Unknown, "ignored");
  TEST_ASSERT_EQUAL_STRING("unknown", line);
  formatOuiLine(line, sizeof(line), OuiState::Local, "Hidden Vendor");
  TEST_ASSERT_EQUAL_STRING("local", line);
  formatOuiLine(line, sizeof(line), OuiState::Group, "Group Vendor");
  TEST_ASSERT_EQUAL_STRING("group", line);
  formatOuiLine(line, sizeof(line), OuiState::DataMissing, "Acme Widgets");
  TEST_ASSERT_EQUAL_STRING("unavailable", line);
  formatOuiLine(line, sizeof(line), OuiState::Unset, "Acme Widgets");
  TEST_ASSERT_EQUAL_STRING("", line);

  UiSnapshot snapshot;
  snapshot.phase = UiPhase::Hosts;
  snapshot.rowPresent[0] = true;
  snapshot.rowPresent[1] = true;
  snapshot.rowPresent[2] = true;
  const char* ip0 = "10.0.0.1";
  const char* ip1 = "10.0.0.2";
  const char* ip2 = "10.0.0.3";
  for (int i = 0; ip0[i] != '\0'; ++i) {
    snapshot.rowLabel[0][i] = ip0[i];
  }
  for (int i = 0; ip1[i] != '\0'; ++i) {
    snapshot.rowLabel[1][i] = ip1[i];
  }
  for (int i = 0; ip2[i] != '\0'; ++i) {
    snapshot.rowLabel[2][i] = ip2[i];
  }
  const char* detail = "m:alpha 00:11:22:33:44:55";
  for (int i = 0; detail[i] != '\0'; ++i) {
    snapshot.rowDetail[0][i] = detail[i];
  }
  formatOuiLine(snapshot.rowVendor[0], sizeof(snapshot.rowVendor[0]), OuiState::Known, "Acme Widgets");
  formatOuiLine(snapshot.rowVendor[1], sizeof(snapshot.rowVendor[1]), OuiState::Unknown, nullptr);
  formatOuiLine(snapshot.rowVendor[2], sizeof(snapshot.rowVendor[2]), OuiState::Local, "Hidden Vendor");
  UiControl controls[8];
  const int count = collectUiControls(controls, 8, snapshot);
  bool sawKnown = false;
  bool sawUnknown = false;
  bool sawLocal = false;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.0.0.1") == 0) {
      sawKnown = true;
      TEST_ASSERT_EQUAL_STRING("m:alpha 00:11:22:33:44:55", controls[i].detail);
      TEST_ASSERT_EQUAL_STRING("Acme Widgets", controls[i].vendor);
    } else if (strcmp(controls[i].label, "10.0.0.2") == 0) {
      sawUnknown = true;
      TEST_ASSERT_EQUAL_STRING("unknown", controls[i].vendor);
    } else if (strcmp(controls[i].label, "10.0.0.3") == 0) {
      sawLocal = true;
      TEST_ASSERT_EQUAL_STRING("local", controls[i].vendor);
    }
  }
  TEST_ASSERT_TRUE(sawKnown && sawUnknown && sawLocal);
}

void test_ui_results_row_uses_role_name(void) {
  UiSnapshot snapshot;
  snapshot.phase = UiPhase::Results;
  snapshot.rowPresent[0] = true;
  const UiGesture gesture = playTap(snapshot, 20, 50, 40, 80);
  TEST_ASSERT_TRUE(gesture.hitId == IdRow0 && gesture.fire);
  TEST_ASSERT_EQUAL_STRING("row", uiControlName(gesture.hitId));
}

static int gPicked = -1;

static void rememberPick(void* context, int index) {
  (void)context;
  gPicked = index;
}

static void act(ScannerController& scanner, AppView& view, bool viaControl, int id, AppAction direct, const AppHooks* hooks) {
  int row = -1;
  const AppAction chosen = viaControl ? actionFromControl(id, &row) : direct;
  if (chosen == AppAction::SelectRow || chosen == AppAction::SetProfile || chosen == AppAction::SetLimit) {
    view.rowOffset = row;
  }
  applyAppAction(chosen, view, scanner, hooks);
}

void test_action_parity_touch_and_direct(void) {
  TEST_ASSERT_TRUE(IdRow0 == 200);
  TEST_ASSERT_TRUE(IdShift == 10);
  TEST_ASSERT_TRUE(IdPrev != IdRow0 + 1);
  int row = -1;
  TEST_ASSERT_TRUE(actionFromControl(IdPrev, &row) == AppAction::PrevPage);
  TEST_ASSERT_TRUE(actionFromControl(IdRow0, &row) == AppAction::SelectRow && row == 0);
  TEST_ASSERT_TRUE(actionFromControl(IdStart, nullptr) == AppAction::StartScan);

  gScanNow = 8000;
  ScannerController touch;
  ScannerController direct;
  FakeDiscoveryBackend touchBackend;
  FakeDiscoveryBackend directBackend;
  armReady(touch, touchBackend, lan24(), 60000);
  armReady(direct, directBackend, lan24(), 60000);
  AppView touchView;
  AppView directView;
  act(touch, touchView, true, IdStart, AppAction::StartScan, nullptr);
  act(direct, directView, false, IdStart, AppAction::StartScan, nullptr);
  TEST_ASSERT_TRUE(touch.state() == ScanState::Starting && direct.state() == ScanState::Starting);

  gScanNow += kScannerTransitionMs;
  touch.loop();
  direct.loop();
  TEST_ASSERT_TRUE(touch.state() == ScanState::Scanning && direct.state() == ScanState::Scanning);
  TEST_ASSERT_TRUE(touch.processedCount() == direct.processedCount());

  act(touch, touchView, true, IdPause, AppAction::PauseScan, nullptr);
  act(direct, directView, false, IdPause, AppAction::PauseScan, nullptr);
  TEST_ASSERT_TRUE(touch.state() == ScanState::Paused && direct.state() == ScanState::Paused);

  act(touch, touchView, true, IdResume, AppAction::ResumeScan, nullptr);
  act(direct, directView, false, IdResume, AppAction::ResumeScan, nullptr);
  TEST_ASSERT_TRUE(touch.state() == ScanState::Scanning && direct.state() == ScanState::Scanning);

  act(touch, touchView, true, IdStop, AppAction::StopScan, nullptr);
  act(direct, directView, false, IdStop, AppAction::StopScan, nullptr);
  gScanNow += kScannerTransitionMs;
  touch.loop();
  direct.loop();
  TEST_ASSERT_TRUE(touch.state() == ScanState::Complete && direct.state() == ScanState::Complete);

  touchView.page = 3;
  directView.page = 3;
  act(touch, touchView, true, IdReset, AppAction::ResetScan, nullptr);
  act(direct, directView, false, IdReset, AppAction::ResetScan, nullptr);
  TEST_ASSERT_TRUE(touch.state() == ScanState::Idle && direct.state() == ScanState::Idle);
  TEST_ASSERT_TRUE(touch.observedCount() == 0 && direct.observedCount() == 0);
  TEST_ASSERT_TRUE(touchView.page == 0 && directView.page == 0);

  touchView.observedCount = 13;
  directView.observedCount = 13;
  act(touch, touchView, true, IdHosts, AppAction::OpenHosts, nullptr);
  act(direct, directView, false, IdHosts, AppAction::OpenHosts, nullptr);
  TEST_ASSERT_TRUE(touchView.showingHosts && directView.showingHosts && touchView.page == 0 && directView.page == 0);
  touchView.observedCount = 13;
  directView.observedCount = 13;
  act(touch, touchView, true, IdNext, AppAction::NextPage, nullptr);
  act(direct, directView, false, IdNext, AppAction::NextPage, nullptr);
  TEST_ASSERT_TRUE(touchView.page == 1 && directView.page == 1);
  act(touch, touchView, true, IdPrev, AppAction::PrevPage, nullptr);
  act(direct, directView, false, IdPrev, AppAction::PrevPage, nullptr);
  TEST_ASSERT_TRUE(touchView.page == 0 && directView.page == 0);
  act(touch, touchView, true, IdBack, AppAction::Back, nullptr);
  act(direct, directView, false, IdBack, AppAction::Back, nullptr);
  TEST_ASSERT_TRUE(!touchView.showingHosts && !directView.showingHosts);

  AppHooks hooks;
  hooks.selectResult = rememberPick;
  gPicked = -1;
  touchView.showingHosts = false;
  touchView.resultsOpen = true;
  touchView.page = 1;
  act(touch, touchView, true, IdRow0 + 2, AppAction::SelectRow, &hooks);
  TEST_ASSERT_TRUE(gPicked == 8);
  gPicked = -1;
  touchView.showingHosts = true;
  act(touch, touchView, false, IdRow0, AppAction::SelectRow, &hooks);
  TEST_ASSERT_TRUE(gPicked == -1);
}

void test_app_state_has_no_secret(void) {
  gScanNow = 9000;
  ScannerController scanner;
  FakeDiscoveryBackend backend;
  armReady(scanner, backend, lan24(), 60000);
  AppView view;
  view.showingHosts = true;
  view.page = 2;
  view.keyboardPage = 1;
  AppWifiView wifi;
  wifi.phase = "Password";
  wifi.ssid = "TFMiddle";
  wifi.saved = true;
  wifi.shift = true;
  wifi.entry = false;
  AppState state;
  fillAppState(state, view, scanner, wifi);
  char line[320];
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "Password") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "psk") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "passphrase") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "screen=hosts") != nullptr);
  TEST_ASSERT_TRUE(strstr(line, "wifi=entry") != nullptr);
  TEST_ASSERT_TRUE(strstr(line, "ssid=TFMiddle") != nullptr);
  TEST_ASSERT_TRUE(strstr(line, "scan=IDLE") != nullptr);
  TEST_ASSERT_TRUE(strstr(line, "canStart=1") != nullptr);
  TEST_ASSERT_TRUE(state.screen == AppScreen::Hosts);
}

struct ExportRam {
  char path[80];
  char body[4096];
  bool used;
};

static ExportRam gExportRam[4];

static ExportRam* findExport(const char* path) {
  for (int i = 0; i < 4; ++i) {
    if (gExportRam[i].used && strcmp(gExportRam[i].path, path) == 0) {
      return &gExportRam[i];
    }
  }
  return nullptr;
}

static bool exportWrite(void* context, const char* path, const char* text) {
  (void)context;
  if (path == nullptr || text == nullptr || strlen(path) >= sizeof(gExportRam[0].path) ||
      strlen(text) >= sizeof(gExportRam[0].body)) {
    return false;
  }
  ExportRam* slot = findExport(path);
  if (slot == nullptr) {
    for (int i = 0; i < 4; ++i) {
      if (!gExportRam[i].used) {
        slot = &gExportRam[i];
        break;
      }
    }
  }
  if (slot == nullptr) {
    return false;
  }
  memset(slot, 0, sizeof(*slot));
  memcpy(slot->path, path, strlen(path));
  memcpy(slot->body, text, strlen(text));
  slot->used = true;
  return true;
}

static bool exportRename(void* context, const char* fromPath, const char* toPath) {
  (void)context;
  ExportRam* src = findExport(fromPath);
  if (src == nullptr || toPath == nullptr || strlen(toPath) >= sizeof(src->path)) {
    return false;
  }
  ExportRam* dest = findExport(toPath);
  if (dest != nullptr && dest != src) {
    dest->used = false;
  }
  memset(src->path, 0, sizeof(src->path));
  memcpy(src->path, toPath, strlen(toPath));
  return true;
}

static void copyField(char* dest, size_t cap, const char* text) {
  size_t n = 0;
  if (text != nullptr) {
    for (; text[n] != '\0' && n + 1 < cap; ++n) {
      dest[n] = text[n];
    }
  }
  dest[n] = '\0';
}

void test_inventory_csv_escape_and_publish(void) {
  InventoryRow rows[6];
  memset(rows, 0, sizeof(rows));
  ObservedHost missing;
  missing.ip = ipv4(10, 0, 0, 2);
  missing.method = "arp";
  missing.macClass = MacClass::Absent;
  missing.ouiState = OuiState::DataMissing;
  missing.manufacturer = "Hidden";
  inventoryRowFromHost(rows[0], missing);
  TEST_ASSERT_TRUE(rows[0].manufacturer[0] == '\0');
  TEST_ASSERT_TRUE(strcmp(rows[0].ouiState, "unavailable") == 0);
  TEST_ASSERT_TRUE(rows[0].mac[0] == '\0');

  copyField(rows[1].ip, sizeof(rows[1].ip), "10.0.0.1");
  copyField(rows[1].mac, sizeof(rows[1].mac), "00:11:22:33:44:55");
  copyField(rows[1].method, sizeof(rows[1].method), "arp");
  copyField(rows[1].name, sizeof(rows[1].name), "a,b");
  copyField(rows[1].nameSource, sizeof(rows[1].nameSource), "mdns");
  copyField(rows[1].macClass, sizeof(rows[1].macClass), "global");
  copyField(rows[1].ouiState, sizeof(rows[1].ouiState), "known");
  copyField(rows[1].manufacturer, sizeof(rows[1].manufacturer), "Acme, Widgets");

  rows[2] = rows[1];
  copyField(rows[2].ip, sizeof(rows[2].ip), "10.0.0.3");
  copyField(rows[2].name, sizeof(rows[2].name), "plain");
  copyField(rows[2].manufacturer, sizeof(rows[2].manufacturer), "Say \"hi\"");

  rows[3] = rows[1];
  copyField(rows[3].ip, sizeof(rows[3].ip), "10.0.0.4");
  copyField(rows[3].name, sizeof(rows[3].name), "Line1\nLine2");
  copyField(rows[3].manufacturer, sizeof(rows[3].manufacturer), "Tail Vendor");

  ObservedHost grouped;
  grouped.ip = ipv4(10, 0, 0, 6);
  grouped.hasMac = true;
  const uint8_t groupMac[6] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
  memcpy(grouped.mac, groupMac, sizeof(groupMac));
  grouped.method = "arp";
  grouped.macClass = MacClass::Group;
  grouped.ouiState = OuiState::Group;
  grouped.manufacturer = "Group Vendor";
  inventoryRowFromHost(rows[4], grouped);
  TEST_ASSERT_TRUE(rows[4].manufacturer[0] == '\0');
  TEST_ASSERT_TRUE(strcmp(rows[4].ouiState, "group") == 0);

  ObservedHost local;
  local.ip = ipv4(10, 0, 0, 7);
  local.hasMac = true;
  const uint8_t localMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  memcpy(local.mac, localMac, sizeof(localMac));
  local.method = "arp";
  local.macClass = MacClass::Local;
  local.ouiState = OuiState::Unknown;
  local.manufacturer = "Local Vendor";
  inventoryRowFromHost(rows[5], local);
  TEST_ASSERT_TRUE(rows[5].manufacturer[0] == '\0');
  TEST_ASSERT_TRUE(strcmp(rows[5].macClass, "local") == 0);
  TEST_ASSERT_TRUE(strcmp(rows[5].ouiState, "unknown") == 0);

  InventoryMeta meta;
  meta.sequence = 1;
  copyField(meta.station, sizeof(meta.station), "10.0.0.5");
  meta.prefix = 28;
  copyField(meta.gateway, sizeof(meta.gateway), "10.0.0.1");
  meta.candidates = 14;
  meta.cap = 256;
  char csv[4096];
  char again[4096];
  TEST_ASSERT_TRUE(formatInventoryCsv(csv, static_cast<int>(sizeof(csv)), meta, rows, 6));
  TEST_ASSERT_TRUE(formatInventoryCsv(again, static_cast<int>(sizeof(again)), meta, rows, 6));
  char preamble[512];
  char rowLine[384];
  TEST_ASSERT_TRUE(formatInventoryPreamble(preamble, static_cast<int>(sizeof(preamble)), meta));
  TEST_ASSERT_TRUE(strncmp(csv, preamble, strlen(preamble)) == 0);
  TEST_ASSERT_TRUE(formatInventoryRowLine(rowLine, static_cast<int>(sizeof(rowLine)), rows[2]));
  TEST_ASSERT_TRUE(strstr(rowLine, "\"Say \"\"hi\"\"\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, rowLine) != nullptr);
  TEST_ASSERT_TRUE(strcmp(csv, again) == 0);
  TEST_ASSERT_TRUE(strstr(csv, "# schema=2\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# sequence=1\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# station=10.0.0.5\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# prefix=28\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# gateway=10.0.0.1\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# candidates=14\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# cap=256\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, inventoryCsvHeader()) == csv + strlen("# schema=2\n# sequence=1\n# station=10.0.0.5\n# prefix=28\n# gateway=10.0.0.1\n# candidates=14\n# cap=256\n"));
  TEST_ASSERT_TRUE(strstr(csv, "\"a,b\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "\"Acme, Widgets\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "\"Say \"\"hi\"\"\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "\"Line1\nLine2\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "Hidden") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "Group Vendor") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "Local Vendor") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "psk") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "passphrase") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "Offline") == nullptr);
  TEST_ASSERT_TRUE(strstr(csv, ",group,") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, ",unknown,") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, ",unavailable,") != nullptr);

  memset(gExportRam, 0, sizeof(gExportRam));
  char path[80];
  TEST_ASSERT_TRUE(inventoryScanPath(path, sizeof(path), 1));
  TEST_ASSERT_TRUE(strcmp(path, "/WiFi-LAN-Scanner/scans/scan-00000001.csv") == 0);
  TEST_ASSERT_TRUE(strstr(path, "/LANScanner/scans/") == nullptr);
  PublishSink sink;
  sink.write = exportWrite;
  sink.rename = exportRename;
  TEST_ASSERT_TRUE(publishText(sink, path, csv));
  char temporary[96];
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  TEST_ASSERT_TRUE(findExport(temporary) == nullptr);
  ExportRam* finalFile = findExport(path);
  TEST_ASSERT_TRUE(finalFile != nullptr && strcmp(finalFile->body, csv) == 0);

  const InventoryStoreResult stored = storeInventoryOnSd();
  TEST_ASSERT_TRUE(stored.status == InventoryStoreStatus::Unavailable);
  TEST_ASSERT_TRUE(stored.detail != nullptr && strcmp(stored.detail, "contract-unproven") == 0);
}

void test_ui_scan_and_persist_copy(void) {
  char banner[80];
  char card[22];
  TEST_ASSERT_TRUE(formatScanBanner(banner, sizeof(banner), ScanState::Idle, false, 0, 0, 0));
  TEST_ASSERT_EQUAL_STRING("Idle | join Wi-Fi before scanning", banner);
  TEST_ASSERT_TRUE(formatScanCard(card, sizeof(card), ScanState::Idle, false, 0, 0));
  TEST_ASSERT_EQUAL_STRING("Idle join Wi-Fi", card);

  TEST_ASSERT_TRUE(formatScanBanner(banner, sizeof(banner), ScanState::Idle, true, 0, 256, 0));
  TEST_ASSERT_EQUAL_STRING("Ready | 0/256 observed 0", banner);
  TEST_ASSERT_TRUE(formatScanCard(card, sizeof(card), ScanState::Idle, true, 0, 256));
  TEST_ASSERT_EQUAL_STRING("Ready 0/256", card);

  TEST_ASSERT_TRUE(formatScanBanner(banner, sizeof(banner), ScanState::Scanning, true, 4, 20, 1));
  TEST_ASSERT_EQUAL_STRING("Scanning | 4/20 observed 1", banner);
  TEST_ASSERT_TRUE(formatScanCard(card, sizeof(card), ScanState::Scanning, true, 4, 20));
  TEST_ASSERT_EQUAL_STRING("Scanning 4/20", card);

  TEST_ASSERT_TRUE(formatScanBanner(banner, sizeof(banner), ScanState::Paused, true, 4, 20, 1));
  TEST_ASSERT_EQUAL_STRING("Paused | 4/20 observed 1", banner);
  TEST_ASSERT_TRUE(formatScanCard(card, sizeof(card), ScanState::Paused, true, 4, 20));
  TEST_ASSERT_EQUAL_STRING("Paused 4/20", card);

  TEST_ASSERT_TRUE(formatScanBanner(banner, sizeof(banner), ScanState::Complete, true, 20, 20, 2));
  TEST_ASSERT_EQUAL_STRING("Complete | 20/20 observed 2", banner);
  TEST_ASSERT_TRUE(formatScanCard(card, sizeof(card), ScanState::Complete, true, 20, 20));
  TEST_ASSERT_EQUAL_STRING("Complete 20/20", card);

  TEST_ASSERT_TRUE(formatScanCard(card, sizeof(card), ScanState::Scanning, true, 256, 256));
  TEST_ASSERT_EQUAL_STRING("Scanning 256/256", card);
  TEST_ASSERT_TRUE(strlen(card) < sizeof(card));

  InventoryStoreResult fresh;
  char panel[48];
  char full[96];
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), fresh));
  TEST_ASSERT_EQUAL_STRING("No save yet", full);
  TEST_ASSERT_TRUE(formatPersistPanel(panel, sizeof(panel), fresh));
  TEST_ASSERT_EQUAL_STRING("No save yet", panel);

  InventoryStoreResult unavailable;
  unavailable.detail = "contract-unproven";
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), unavailable));
  TEST_ASSERT_EQUAL_STRING("No save yet", full);

  InventoryStoreResult absent;
  absent.status = InventoryStoreStatus::Absent;
  absent.detail = "media-absent";
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), absent));
  TEST_ASSERT_EQUAL_STRING("No SD", full);

  InventoryStoreResult failed;
  failed.status = InventoryStoreStatus::Failed;
  failed.detail = "write";
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), failed));
  TEST_ASSERT_EQUAL_STRING("Save failed", full);

  InventoryStoreResult stored;
  stored.status = InventoryStoreStatus::Stored;
  stored.detail = "stored";
  snprintf(stored.path, sizeof(stored.path), "/WiFi-LAN-Scanner/scans/scan-00000001.csv");
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), stored));
  TEST_ASSERT_EQUAL_STRING("SD stored /WiFi-LAN-Scanner/scans/scan-00000001.csv", full);
  TEST_ASSERT_TRUE(formatPersistPanel(panel, sizeof(panel), stored));
  TEST_ASSERT_EQUAL_STRING("SD Saved", panel);
  TEST_ASSERT_TRUE(strlen(panel) <= 34);
  TEST_ASSERT_TRUE(strlen(panel) < strlen(full));
}

struct RemoteWorld {
  ScannerController* scanner = nullptr;
  AppView* view = nullptr;
  int applies = 0;
  AppAction last = AppAction::None;
  int lastRow = -1;
  InventoryRow rows[2];
  int rowsCount = 0;
  AppState state;
  bool haveState = false;
};

static bool worldApply(void* context, AppAction action, int rowOffset, const char* text) {
  auto* world = static_cast<RemoteWorld*>(context);
  if (world == nullptr || world->scanner == nullptr || world->view == nullptr) {
    return false;
  }
  world->applies += 1;
  world->last = action;
  world->lastRow = rowOffset;
  if (action == AppAction::SelectRow || action == AppAction::SetProfile || action == AppAction::SetLimit) {
    world->view->rowOffset = rowOffset;
  }
  return applyAppAction(action, *world->view, *world->scanner, nullptr, text);
}

static void worldLoad(void* context, AppState* out) {
  auto* world = static_cast<RemoteWorld*>(context);
  if (out == nullptr) {
    return;
  }
  if (world != nullptr && world->haveState) {
    *out = world->state;
    return;
  }
  if (world == nullptr || world->scanner == nullptr || world->view == nullptr) {
    *out = AppState();
    return;
  }
  AppWifiView wifi;
  wifi.phase = "connected";
  wifi.ssid = "TFMiddle";
  wifi.saved = false;
  fillAppState(*out, *world->view, *world->scanner, wifi, world->view->profile);
}

static int worldRows(void* context) {
  auto* world = static_cast<RemoteWorld*>(context);
  if (world == nullptr || world->rowsCount < 0) {
    return 0;
  }
  return world->rowsCount;
}

static bool worldRow(void* context, int index, InventoryRow* out) {
  auto* world = static_cast<RemoteWorld*>(context);
  if (world == nullptr || out == nullptr || index < 0 || index >= world->rowsCount || index >= 2) {
    return false;
  }
  *out = world->rows[index];
  return true;
}

static RemoteServices worldServices(RemoteWorld& world) {
  RemoteServices services;
  services.apply = worldApply;
  services.loadState = worldLoad;
  services.rowCount = worldRows;
  services.rowAt = worldRow;
  services.context = &world;
  return services;
}

static int submitWorld(RemoteSession& session, RemoteWorld& world, const char* line, char* out, int cap) {
  const RemoteServices services = worldServices(world);
  return remoteSubmit(&session, line, out, cap, &services);
}

static int pullWorld(RemoteSession& session, RemoteWorld& world, char* out, int cap) {
  const RemoteServices services = worldServices(world);
  return remotePull(&session, out, cap, &services);
}

static void requireHello(RemoteSession& session, RemoteWorld& world) {
  char out[640];
  const int n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"HELLO\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"HELLO_ACK\",\"ok\":1,\"link\":\"usb\",\"support\":1}\n", out);
  TEST_ASSERT_TRUE(session.link == RemoteLink::ConnectedUsb);
}

static void requireAction(RemoteSession& session, RemoteWorld& world, const char* name) {
  char out[640];
  char line[128];
  char expect[128];
  snprintf(line, sizeof(line), "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"%s\"}", name);
  snprintf(expect, sizeof(expect), "@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"%s\",\"ok\":1}\n", name);
  const int n = submitWorld(session, world, line, out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING(expect, out);
}

void test_remote_session_and_state(void) {
  RemoteWorld world;
  world.haveState = true;
  world.state.screen = AppScreen::Home;
  snprintf(world.state.wifiPhase, sizeof(world.state.wifiPhase), "connected");
  memset(world.state.ssid, 'S', 32);
  world.state.ssid[30] = '"';
  world.state.ssid[32] = '\0';
  snprintf(world.state.scan, sizeof(world.state.scan), "IDLE");
  world.state.processed = 256;
  world.state.candidates = 256;
  world.state.observed = 3;
  snprintf(world.state.current, sizeof(world.state.current), "10.28.255.254");
  snprintf(world.state.last, sizeof(world.state.last), "10.28.255.254");
  snprintf(world.state.newest, sizeof(world.state.newest), "10.28.0.12");
  world.state.elapsedMs = 180000;
  world.state.canStart = true;

  RemoteSession session;
  char out[640];
  int n = submitWorld(session, world, "@R1 {\"v\":2,\"op\":\"HELLO\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"HELLO_ACK\",\"ok\":0,\"link\":\"none\",\"support\":1}\n", out);
  TEST_ASSERT_TRUE(session.link == RemoteLink::Disconnected);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"closed\"}\n", out);

  requireHello(session, world);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"HELLO\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"session\"}\n", out);
  TEST_ASSERT_TRUE(session.link == RemoteLink::ConnectedUsb);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(n > 0 && n < 576);
  TEST_ASSERT_TRUE(strncmp(out, "@R1 {\"v\":1,\"op\":\"STATE\"", 22) == 0);
  TEST_ASSERT_TRUE(strstr(out, "\\\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"scan\":\"IDLE\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"screen\":\"home\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(out, "passphrase") == nullptr);
  TEST_ASSERT_TRUE(strstr(out, "psk") == nullptr);
  TEST_ASSERT_TRUE(strncmp(out, "WLS", 3) != 0);

  n = submitWorld(session, world, "@R1 {", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"malformed\"}\n", out);
  TEST_ASSERT_TRUE(session.link == RemoteLink::ConnectedUsb);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"PING\",\"nested\":{\"a\":1}}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"malformed\"}\n", out);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"SHELL\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"unknown\"}\n", out);

  char huge[360];
  memcpy(huge, "@R1 ", 4);
  memset(huge + 4, 'B', 330);
  huge[334] = '\0';
  TEST_ASSERT_TRUE(strlen(huge) > static_cast<size_t>(kRemoteMaxLine));
  n = submitWorld(session, world, huge, out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"oversize\"}\n", out);
  TEST_ASSERT_TRUE(session.link == RemoteLink::ConnectedUsb);
  TEST_ASSERT_FALSE(session.streaming);

  n = remoteMarkOversize(&session, out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"oversize\"}\n", out);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"PING\",\"id\":7}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"PONG\",\"id\":7}\n", out);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GOODBYE\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"GOODBYE\",\"ok\":1}\n", out);
  TEST_ASSERT_TRUE(session.link == RemoteLink::Disconnected);

  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"PING\",\"id\":7}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"closed\"}\n", out);
  requireHello(session, world);
}

void test_remote_touch_parity(void) {
  gScanNow = 12000;
  ScannerController touchScanner;
  ScannerController remoteScanner;
  FakeDiscoveryBackend touchBackend;
  FakeDiscoveryBackend remoteBackend;
  armReady(touchScanner, touchBackend, lan24(), 60000);
  armReady(remoteScanner, remoteBackend, lan24(), 60000);
  AppView touchView;
  AppView remoteView;
  RemoteWorld world;
  world.scanner = &remoteScanner;
  world.view = &remoteView;
  RemoteSession session;
  requireHello(session, world);

  act(touchScanner, touchView, true, IdStart, AppAction::StartScan, nullptr);
  requireAction(session, world, "start");
  TEST_ASSERT_TRUE(touchScanner.state() == ScanState::Starting && remoteScanner.state() == ScanState::Starting);

  gScanNow += kScannerTransitionMs;
  touchScanner.loop();
  remoteScanner.loop();
  TEST_ASSERT_TRUE(touchScanner.state() == ScanState::Scanning && remoteScanner.state() == ScanState::Scanning);
  TEST_ASSERT_TRUE(touchScanner.processedCount() == remoteScanner.processedCount());

  act(touchScanner, touchView, true, IdPause, AppAction::PauseScan, nullptr);
  requireAction(session, world, "pause");
  TEST_ASSERT_TRUE(touchScanner.state() == ScanState::Paused && remoteScanner.state() == ScanState::Paused);

  act(touchScanner, touchView, true, IdResume, AppAction::ResumeScan, nullptr);
  requireAction(session, world, "resume");
  TEST_ASSERT_TRUE(touchScanner.state() == ScanState::Scanning && remoteScanner.state() == ScanState::Scanning);

  act(touchScanner, touchView, true, IdStop, AppAction::StopScan, nullptr);
  requireAction(session, world, "stop");
  gScanNow += kScannerTransitionMs;
  touchScanner.loop();
  remoteScanner.loop();
  TEST_ASSERT_TRUE(touchScanner.state() == ScanState::Complete && remoteScanner.state() == ScanState::Complete);

  act(touchScanner, touchView, true, IdReset, AppAction::ResetScan, nullptr);
  requireAction(session, world, "reset");
  TEST_ASSERT_TRUE(touchScanner.state() == ScanState::Idle && remoteScanner.state() == ScanState::Idle);
  TEST_ASSERT_TRUE(touchScanner.observedCount() == 0 && remoteScanner.observedCount() == 0);
  TEST_ASSERT_TRUE(touchView.page == 0 && remoteView.page == 0);

  touchView.observedCount = 13;
  remoteView.observedCount = 13;
  act(touchScanner, touchView, true, IdHosts, AppAction::OpenHosts, nullptr);
  requireAction(session, world, "hosts");
  TEST_ASSERT_TRUE(touchView.showingHosts && remoteView.showingHosts);
  TEST_ASSERT_TRUE(touchView.page == 0 && remoteView.page == 0);
  touchView.observedCount = 13;
  remoteView.observedCount = 13;
  act(touchScanner, touchView, true, IdNext, AppAction::NextPage, nullptr);
  requireAction(session, world, "next");
  TEST_ASSERT_TRUE(touchView.page == 1 && remoteView.page == 1);
  act(touchScanner, touchView, true, IdPrev, AppAction::PrevPage, nullptr);
  requireAction(session, world, "prev");
  TEST_ASSERT_TRUE(touchView.page == 0 && remoteView.page == 0);
  act(touchScanner, touchView, true, IdBack, AppAction::Back, nullptr);
  requireAction(session, world, "back");
  TEST_ASSERT_TRUE(!touchView.showingHosts && !remoteView.showingHosts);

  const int before = world.applies;
  char out[640];
  const int unknown = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"shell\"}", out,
                                  static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(unknown > 0);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"unknown\"}\n", out);
  TEST_ASSERT_TRUE(world.applies == before);
  TEST_ASSERT_TRUE(remoteScanner.state() == ScanState::Idle);

  const int range = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"row\",\"index\":9}", out,
                                static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(range > 0);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"range\"}\n", out);
  TEST_ASSERT_TRUE(world.applies == before);

  const int row = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"row\",\"index\":2}", out,
                              static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(row > 0);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"row\",\"ok\":1}\n", out);
  TEST_ASSERT_TRUE(world.applies == before + 1);
  TEST_ASSERT_TRUE(world.last == AppAction::SelectRow && world.lastRow == 2);

  const int extra = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"pause\",\"index\":3}", out,
                                static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(extra > 0);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"pause\",\"ok\":1}\n", out);
  TEST_ASSERT_TRUE(world.last == AppAction::PauseScan && world.lastRow == -1);
  TEST_ASSERT_TRUE(remoteScanner.state() == ScanState::Idle);
}

void test_remote_result_rows(void) {
  RemoteWorld world;
  ObservedHost named;
  named.ip = ipv4(10, 28, 0, 11);
  named.hasMac = true;
  const uint8_t namedMac[6] = {0x00, 0x30, 0x18, 0xCB, 0xF9, 0x8E};
  memcpy(named.mac, namedMac, sizeof(namedMac));
  named.method = "arp";
  snprintf(named.name, sizeof(named.name), "gw");
  named.nameSource = NameSource::Mdns;
  named.macClass = MacClass::Global;
  named.ouiState = OuiState::Known;
  named.manufacturer = "Say \"hi\"";
  inventoryRowFromHost(world.rows[0], named);

  ObservedHost bare;
  bare.ip = ipv4(10, 28, 0, 12);
  bare.method = "arp";
  bare.macClass = MacClass::Global;
  bare.ouiState = OuiState::Unknown;
  inventoryRowFromHost(world.rows[1], bare);
  world.rowsCount = 2;

  RemoteSession session;
  char first[640];
  char second[640];
  char end[640];
  requireHello(session, world);
  const int firstN = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_RESULTS\"}", first, static_cast<int>(sizeof(first)));
  TEST_ASSERT_TRUE(firstN > 0);
  TEST_ASSERT_TRUE(session.streaming);
  TEST_ASSERT_TRUE(strstr(first, "\"op\":\"RESULT_ROW\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(first, "\"i\":0") != nullptr);
  TEST_ASSERT_TRUE(strstr(first, world.rows[0].ip) != nullptr);
  TEST_ASSERT_TRUE(strstr(first, world.rows[0].mac) != nullptr);
  TEST_ASSERT_TRUE(strstr(first, world.rows[0].method) != nullptr);
  TEST_ASSERT_TRUE(strstr(first, world.rows[0].name) != nullptr);
  TEST_ASSERT_TRUE(strstr(first, "Say \\\"hi\\\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(first, "psk") == nullptr);
  TEST_ASSERT_TRUE(strstr(first, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(first, "passphrase") == nullptr);

  const int busy = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"PING\",\"id\":1}", end, static_cast<int>(sizeof(end)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"busy\"}\n", end);
  TEST_ASSERT_TRUE(busy > 0);

  const int secondN = pullWorld(session, world, second, static_cast<int>(sizeof(second)));
  TEST_ASSERT_TRUE(secondN > 0);
  TEST_ASSERT_TRUE(strstr(second, "\"i\":1") != nullptr);
  TEST_ASSERT_TRUE(strstr(second, world.rows[1].ip) != nullptr);
  TEST_ASSERT_TRUE(strstr(second, "\"mac\":\"\"") != nullptr);
  const int endN = pullWorld(session, world, end, static_cast<int>(sizeof(end)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"RESULT_END\",\"count\":2}\n", end);
  TEST_ASSERT_TRUE(endN > 0);
  TEST_ASSERT_FALSE(session.streaming);
  TEST_ASSERT_TRUE(pullWorld(session, world, end, static_cast<int>(sizeof(end))) == 0);

  const int recovered = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"PING\",\"id\":4}", end, static_cast<int>(sizeof(end)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"PONG\",\"id\":4}\n", end);
  TEST_ASSERT_TRUE(recovered > 0);

  char again[640];
  const int againN = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_RESULTS\"}", again, static_cast<int>(sizeof(again)));
  TEST_ASSERT_TRUE(againN > 0);
  TEST_ASSERT_EQUAL_STRING(first, again);
  TEST_ASSERT_TRUE(pullWorld(session, world, second, static_cast<int>(sizeof(second))) > 0);
  TEST_ASSERT_TRUE(pullWorld(session, world, end, static_cast<int>(sizeof(end))) > 0);

  world.rowsCount = 0;
  const int empty = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_RESULTS\"}", end, static_cast<int>(sizeof(end)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"RESULT_END\",\"count\":0}\n", end);
  TEST_ASSERT_TRUE(empty > 0);
  TEST_ASSERT_FALSE(session.streaming);
}

struct NavTrace {
  int finds;
  int closes;
  int cancels;
  const char* phase;
};

static void navFind(void* context) {
  auto* nav = static_cast<NavTrace*>(context);
  nav->finds++;
  nav->phase = "results";
}

static void navClose(void* context) {
  auto* nav = static_cast<NavTrace*>(context);
  nav->closes++;
  nav->phase = "home";
}

static void navCancel(void* context) {
  auto* nav = static_cast<NavTrace*>(context);
  nav->cancels++;
  nav->phase = "results";
}

void test_networks_back_returns_home(void) {
  NavTrace nav{0, 0, 0, "home"};
  AppHooks hooks;
  hooks.findNetworks = navFind;
  hooks.closeResults = navClose;
  hooks.cancelPassword = navCancel;
  hooks.context = &nav;
  gScanNow = 12000;
  ScannerController touch;
  ScannerController direct;
  FakeDiscoveryBackend touchBackend;
  FakeDiscoveryBackend directBackend;
  armReady(touch, touchBackend, lan24(), 60000);
  armReady(direct, directBackend, lan24(), 60000);
  AppView touchView;
  AppView directView;

  act(touch, touchView, true, IdFind, AppAction::FindNetworks, &hooks);
  TEST_ASSERT_TRUE(nav.finds == 1);
  TEST_ASSERT_TRUE(strcmp(nav.phase, "results") == 0);
  touchView.resultsOpen = true;
  touchView.page = 2;
  act(touch, touchView, true, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(nav.closes == 1 && nav.cancels == 0);
  TEST_ASSERT_TRUE(strcmp(nav.phase, "home") == 0);
  TEST_ASSERT_TRUE(touchView.page == 0 && !touchView.showingHosts);

  nav.phase = "results";
  directView.resultsOpen = true;
  directView.page = 1;
  act(direct, directView, false, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(nav.closes == 2 && nav.cancels == 0);
  TEST_ASSERT_TRUE(strcmp(nav.phase, "home") == 0);
  TEST_ASSERT_TRUE(directView.page == 0);

  AppView hosts;
  hosts.showingHosts = true;
  hosts.page = 1;
  act(touch, hosts, true, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(!hosts.showingHosts && hosts.page == 0);
  TEST_ASSERT_TRUE(nav.closes == 2 && nav.cancels == 0);

  AppView entry;
  act(touch, entry, false, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(nav.cancels == 1 && nav.closes == 2);

  touchView.resultsOpen = false;
  act(touch, touchView, true, IdFind, AppAction::FindNetworks, &hooks);
  TEST_ASSERT_TRUE(nav.finds == 2);
  TEST_ASSERT_TRUE(strcmp(nav.phase, "results") == 0);
}

static bool controlPresent(const UiControl* controls, int count, int id) {
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == id) {
      return true;
    }
  }
  return false;
}

void test_remote_visual_ack_matches_touch_face(void) {
  UiControl controls[16];
  UiSnapshot home;
  home.phase = UiPhase::Home;
  home.showDashboard = true;
  home.scan = ScanState::Idle;
  int count = collectUiControls(controls, 16, home);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::FindNetworks, -1, controls, count) == IdFind);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::Back, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::StartScan, -1, controls, count) == IdStart);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::PauseScan, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::ResumeScan, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::StopScan, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::ResetScan, -1, controls, count) == IdReset);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::OpenHosts, -1, controls, count) == IdHosts);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::OpenSettings, -1, controls, count) == IdSettings);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::NextPage, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::PrevPage, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SelectRow, 0, controls, count) == -1);
  TEST_ASSERT_TRUE(controlPresent(controls, count, IdFind));
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdFind) {
      TEST_ASSERT_TRUE(controls[i].x == 8 && controls[i].y == 72 && controls[i].w == 206 && controls[i].h == 34);
    }
    if (controls[i].id == IdReset) {
      TEST_ASSERT_TRUE(controls[i].dim && !controls[i].secondary);
    }
    if (controls[i].id == IdSettings) {
      TEST_ASSERT_TRUE(controls[i].secondary && !controls[i].dim);
    }
    if (controls[i].id == IdStart) {
      TEST_ASSERT_TRUE(!controls[i].dim && !controls[i].secondary);
    }
  }

  UiSnapshot scanning = home;
  scanning.scan = ScanState::Scanning;
  count = collectUiControls(controls, 16, scanning);
  TEST_ASSERT_TRUE(controlPresent(controls, count, IdPause));
  TEST_ASSERT_TRUE(controlPresent(controls, count, IdStop));
  TEST_ASSERT_TRUE(!controlPresent(controls, count, IdResume));
  TEST_ASSERT_TRUE(!controlPresent(controls, count, IdStart));
  TEST_ASSERT_TRUE(!controlPresent(controls, count, IdSettings));
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::PauseScan, -1, controls, count) == IdPause);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::ResumeScan, -1, controls, count) == -1);

  UiSnapshot paused = home;
  paused.scan = ScanState::Paused;
  count = collectUiControls(controls, 16, paused);
  TEST_ASSERT_TRUE(controlPresent(controls, count, IdResume));
  TEST_ASSERT_TRUE(controlPresent(controls, count, IdStop));
  TEST_ASSERT_TRUE(!controlPresent(controls, count, IdPause));
  TEST_ASSERT_TRUE(!controlPresent(controls, count, IdStart));
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::ResumeScan, -1, controls, count) == IdResume);

  UiSnapshot networks;
  networks.phase = UiPhase::Results;
  networks.rowPresent[0] = true;
  snprintf(networks.rowLabel[0], sizeof(networks.rowLabel[0]), "Office");
  count = collectUiControls(controls, 16, networks);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::Back, -1, controls, count) == IdBack);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::FindNetworks, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::NextPage, -1, controls, count) == IdNext);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::PrevPage, -1, controls, count) == IdPrev);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SelectRow, 0, controls, count) == IdRow0);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SelectRow, 1, controls, count) == -1);

  UiSnapshot hosts;
  hosts.phase = UiPhase::Hosts;
  hosts.rowPresent[0] = true;
  snprintf(hosts.rowLabel[0], sizeof(hosts.rowLabel[0]), "10.0.0.1");
  count = collectUiControls(controls, 16, hosts);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::Back, -1, controls, count) == IdBack);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SelectRow, 0, controls, count) == IdRow0);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::FindNetworks, -1, controls, count) == -1);

  ActionAck ack;
  TEST_ASSERT_TRUE(ack.arm(IdFind, 1000));
  TEST_ASSERT_TRUE(ack.pending() && ack.shownId() == IdFind);
  TEST_ASSERT_TRUE(!ack.arm(IdBack, 1010));
  TEST_ASSERT_TRUE(ack.shownId() == IdFind);
  TEST_ASSERT_TRUE(!ack.consume(1119));
  TEST_ASSERT_TRUE(ack.pending());
  TEST_ASSERT_TRUE(ack.consume(1120));
  TEST_ASSERT_TRUE(!ack.pending());
  TEST_ASSERT_TRUE(!ack.consume(1120));
  TEST_ASSERT_TRUE(ack.arm(IdBack, 2000));
  TEST_ASSERT_TRUE(ack.consume(2120));
  TEST_ASSERT_TRUE(ActionAck::kAckMs == PressTracker::kAckMs);
  TEST_ASSERT_TRUE(ActionAck::kAckMs == 120);
  TEST_ASSERT_TRUE(controlFace(false, true) == ControlFace::Pressed);
  TEST_ASSERT_EQUAL_STRING("pressed", faceToken(controlFace(false, true)));
  TEST_ASSERT_EQUAL_STRING("latchedpressed", faceToken(controlFace(true, true)));
}

static bool gRejectBusy = false;
static int gBusyApplies = 0;

static bool busyApply(void* context, AppAction action, int rowOffset, const char* text) {
  (void)context;
  (void)action;
  (void)rowOffset;
  (void)text;
  gBusyApplies++;
  return !gRejectBusy;
}

static bool busyQuery(void* context) {
  (void)context;
  return gRejectBusy;
}

void test_remote_action_busy_result(void) {
  RemoteServices services;
  services.apply = busyApply;
  services.rejectedBusy = busyQuery;
  RemoteSession session;
  char out[160];
  gRejectBusy = false;
  gBusyApplies = 0;
  int n = remoteSubmit(&session, "@R1 {\"v\":1,\"op\":\"HELLO\"}", out, static_cast<int>(sizeof(out)), &services);
  TEST_ASSERT_TRUE(n > 0);
  n = remoteSubmit(&session, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"find\"}", out, static_cast<int>(sizeof(out)),
                   &services);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"find\",\"ok\":1}\n", out);
  TEST_ASSERT_TRUE(gBusyApplies == 1);
  gRejectBusy = true;
  n = remoteSubmit(&session, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"back\"}", out, static_cast<int>(sizeof(out)),
                   &services);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"back\",\"ok\":0,\"reason\":\"busy\"}\n", out);
  TEST_ASSERT_TRUE(gBusyApplies == 2);
  TEST_ASSERT_TRUE(session.link == RemoteLink::ConnectedUsb);
}

void test_dirty_regions_and_progress(void) {
  TEST_ASSERT_TRUE(progressPercent(0, 0) == 0);
  TEST_ASSERT_TRUE(progressPercent(0, 256) == 0);
  TEST_ASSERT_TRUE(progressPercent(128, 256) == 50);
  TEST_ASSERT_TRUE(progressPercent(1, 256) == 0);
  TEST_ASSERT_TRUE(progressPercent(300, 256) == 100);

  int covered = 0;
  static const uint32_t bits[] = {UiRegionHeader, UiRegionWifiActions, UiRegionNetwork, UiRegionProgress,
                                   UiRegionLatest, UiRegionControls,    UiRegionFooter};
  for (uint32_t bit : bits) {
    const UiRegionRect rect = uiRegionRect(bit);
    TEST_ASSERT_TRUE(rect.w == kPanelWidth);
    TEST_ASSERT_TRUE(rect.h > 0 && rect.h <= kUiSpriteH);
    covered += rect.h;
  }
  TEST_ASSERT_TRUE(covered == kPanelHeight);
  const UiRegionRect progress = uiRegionRect(UiRegionProgress);
  TEST_ASSERT_TRUE(progress.y == 240 && progress.h == 46);
  const UiRegionRect findBand = uiRegionRect(UiRegionWifiActions);
  TEST_ASSERT_TRUE(72 >= findBand.y && 72 + 34 <= findBand.y + findBand.h);

  UiPaintFrame prev;
  UiPaintFrame next = prev;
  next.processed = 2;
  next.candidates = 256;
  const uint32_t progressOnly = dirtyRegions(prev, next);
  TEST_ASSERT_TRUE(progressOnly == (UiRegionProgress | UiRegionNetwork));
  TEST_ASSERT_TRUE((progressOnly & UiRegionControls) == 0);
  TEST_ASSERT_TRUE((progressOnly & UiRegionHeader) == 0);

  next = prev;
  next.station = true;
  prev.station = true;
  next.processed = 4;
  next.candidates = 8;
  TEST_ASSERT_TRUE(dirtyRegions(prev, next) == UiRegionProgress);

  next = prev;
  next.observed = 3;
  snprintf(next.newest, sizeof(next.newest), "10.0.0.1");
  const uint32_t latest = dirtyRegions(prev, next);
  TEST_ASSERT_TRUE((latest & UiRegionLatest) != 0);
  TEST_ASSERT_TRUE((latest & UiRegionProgress) == 0);
  TEST_ASSERT_TRUE((latest & UiRegionControls) == 0);

  next = prev;
  next.shownId = IdFind;
  next.shownY = 72;
  next.shownH = 34;
  TEST_ASSERT_TRUE(dirtyRegions(prev, next) == UiRegionWifiActions);

  next = prev;
  next.screen = UiPaintScreen::Results;
  TEST_ASSERT_TRUE(dirtyRegions(prev, next) == UiRegionAll);

  next = prev;
  next.screen = UiPaintScreen::Settings;
  next.shownId = IdProfileBasic;
  next.shownY = 78;
  next.shownH = 60;
  prev.screen = UiPaintScreen::Settings;
  TEST_ASSERT_TRUE(dirtyRegions(prev, next) == UiRegionInPlace);

  next = prev;
  next.profile = 0;
  TEST_ASSERT_TRUE(dirtyRegions(prev, next) == UiRegionAll);
}

static int gProfileSets = 0;
static int gProfileWhich = -1;

static void rememberProfile(void* context, int which) {
  (void)context;
  gProfileSets += 1;
  gProfileWhich = which;
}

void test_settings_and_service_profile(void) {
  TEST_ASSERT_TRUE(defaultServiceProfile() == ServiceProfile::Common);
  const ServiceProfileValue missing = parseServiceProfile(nullptr);
  TEST_ASSERT_TRUE(!missing.valid && missing.profile == ServiceProfile::Common);
  const ServiceProfileValue empty = parseServiceProfile("");
  TEST_ASSERT_TRUE(!empty.valid && empty.profile == ServiceProfile::Common);
  const ServiceProfileValue junk = parseServiceProfile("probe");
  TEST_ASSERT_TRUE(!junk.valid && junk.profile == ServiceProfile::Common);
  const ServiceProfileValue basic = parseServiceProfile("basic");
  TEST_ASSERT_TRUE(basic.valid && basic.profile == ServiceProfile::Basic);
  TEST_ASSERT_EQUAL_STRING("Basic / fast", serviceProfileLabel(ServiceProfile::Basic));
  TEST_ASSERT_EQUAL_STRING("Common / recommended", serviceProfileLabel(ServiceProfile::Common));
  TEST_ASSERT_EQUAL_STRING("Detailed / slower", serviceProfileLabel(ServiceProfile::Detailed));

  UiSnapshot settings;
  settings.phase = UiPhase::Settings;
  settings.settingsPage = SettingsPage::Service;
  settings.profile = ServiceProfile::Common;
  UiControl controls[8];
  int count = collectUiControls(controls, 8, settings);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SetProfile, 1, controls, count) == IdProfileCommon);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SetProfile, 0, controls, count) == IdProfileBasic);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::Back, -1, controls, count) == IdBack);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::FindNetworks, -1, controls, count) == -1);
  bool commonLatched = false;
  bool basicLatched = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdProfileCommon) {
      commonLatched = controls[i].latched;
      TEST_ASSERT_EQUAL_STRING("Common / recommended", controls[i].label);
    }
    if (controls[i].id == IdProfileBasic) {
      basicLatched = controls[i].latched;
    }
    if (controls[i].id == IdBack) {
      TEST_ASSERT_TRUE(controls[i].y == 410 && controls[i].w == 210 && controls[i].h == 40);
    }
  }
  TEST_ASSERT_TRUE(commonLatched && !basicLatched);

  NavTrace nav{0, 0, 0, "home"};
  AppHooks hooks;
  hooks.closeResults = navClose;
  hooks.cancelPassword = navCancel;
  hooks.setProfile = rememberProfile;
  hooks.context = &nav;
  ScannerController scanner;
  AppView settingsView;
  settingsView.showingSettings = true;
  settingsView.page = 2;
  settingsView.resultsOpen = true;
  act(scanner, settingsView, true, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(!settingsView.showingSettings && settingsView.page == 0);
  TEST_ASSERT_TRUE(nav.closes == 0 && nav.cancels == 0);

  AppView servicePage;
  servicePage.showingSettings = true;
  servicePage.settingsPage = SettingsPage::Service;
  servicePage.resultsOpen = true;
  act(scanner, servicePage, true, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(servicePage.showingSettings && servicePage.settingsPage == SettingsPage::Menu);
  TEST_ASSERT_TRUE(nav.closes == 0 && nav.cancels == 0);

  AppView blocked;
  blocked.resultsOpen = true;
  act(scanner, blocked, true, IdSettings, AppAction::OpenSettings, &hooks);
  TEST_ASSERT_TRUE(!blocked.showingSettings);

  AppView home;
  act(scanner, home, true, IdSettings, AppAction::OpenSettings, &hooks);
  TEST_ASSERT_TRUE(home.showingSettings && !home.showingHosts);
  act(scanner, home, true, IdHosts, AppAction::OpenHosts, &hooks);
  TEST_ASSERT_TRUE(home.showingHosts && !home.showingSettings);
  act(scanner, home, true, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(!home.showingHosts);

  AppView picked;
  gProfileSets = 0;
  gProfileWhich = -1;
  act(scanner, picked, true, IdProfileDetailed, AppAction::SetProfile, &hooks);
  TEST_ASSERT_TRUE(picked.profile == ServiceProfile::Detailed);
  TEST_ASSERT_TRUE(gProfileSets == 1 && gProfileWhich == 2);
  picked.rowOffset = -1;
  applyAppAction(AppAction::SetProfile, picked, scanner, &hooks);
  TEST_ASSERT_TRUE(picked.profile == ServiceProfile::Detailed);
  TEST_ASSERT_TRUE(gProfileSets == 1);

  AppState state;
  AppWifiView wifi;
  wifi.phase = "idle";
  wifi.ssid = "TFMiddle";
  fillAppState(state, picked, scanner, wifi, picked.profile);
  TEST_ASSERT_EQUAL_STRING("detailed", state.profile);
  TEST_ASSERT_TRUE(state.screen == AppScreen::Home);
  char line[240];
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "psk") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "passphrase") == nullptr);

  picked.showingSettings = true;
  fillAppState(state, picked, scanner, wifi, picked.profile);
  TEST_ASSERT_TRUE(state.screen == AppScreen::Settings);
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "screen=settings") != nullptr);

  RemoteWorld world;
  world.scanner = &scanner;
  world.view = &picked;
  picked.showingSettings = false;
  picked.profile = ServiceProfile::Common;
  RemoteSession session;
  char out[640];
  requireHello(session, world);
  int n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(n > 0 && n < 576);
  TEST_ASSERT_TRUE(strstr(out, "\"profile\":\"common\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"range\":\"automatic\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"rangeLimit\":256") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"screen\":\"home\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"ack\":\"\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(out, "psk") == nullptr);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"basic\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"basic\",\"ok\":1}\n", out);
  TEST_ASSERT_TRUE(picked.profile == ServiceProfile::Basic);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(strstr(out, "\"profile\":\"basic\"") != nullptr);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"probe\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ERR\",\"reason\":\"unknown\"}\n", out);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"find\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"find\",\"ok\":1}\n", out);
}

void test_resource_line_injected(void) {
  ResourceSample sample;
  sample.heap = 1000;
  sample.minHeap = 800;
  sample.maxBlock = 700;
  sample.psram = 8388608;
  sample.freePsram = 7000000;
  sample.minPsram = 6900000;
  char line[180];
  TEST_ASSERT_TRUE(formatResourceLine(line, static_cast<int>(sizeof(line)), "boot", sample) > 0);
  TEST_ASSERT_TRUE(strcmp(line, "WLS resource phase=boot heap=1000 min=800 block=700 psram=8388608 freePsram=7000000 minPsram=6900000") == 0);
  TEST_ASSERT_TRUE(formatResourceLine(line, static_cast<int>(sizeof(line)), "bad phase", sample) > 0);
  TEST_ASSERT_TRUE(strstr(line, "phase=badphase") != nullptr);
}

static NetFacts slash16Facts() {
  return deriveNetFacts(ipv4(10, 1, 0, 9), ipv4(255, 255, 0, 0), ipv4(10, 1, 5, 5), ipv4(10, 1, 0, 1), ipv4(0, 0, 0, 0));
}

void test_control_affordance_and_address_range(void) {
  char label[22];
  TEST_ASSERT_TRUE(formatAddressProgressLabel(label, sizeof(label), 256, 256));
  TEST_ASSERT_EQUAL_STRING("Addresses 256/256", label);
  TEST_ASSERT_TRUE(strlen(label) <= 21);
  TEST_ASSERT_TRUE(formatDevicesFoundLabel(label, sizeof(label), 3));
  TEST_ASSERT_EQUAL_STRING("Devices found 3", label);

  UiSnapshot home;
  home.phase = UiPhase::Home;
  home.showDashboard = true;
  home.scan = ScanState::Idle;
  const UiGesture progressTap = playTap(home, 100, 260, 40, 80);
  TEST_ASSERT_TRUE(progressTap.hitId < 0);
  UiControl controls[40];
  int count = collectUiControls(controls, 40, home);
  bool progressChrome = false;
  bool newestChrome = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdProgress) {
      progressChrome = controls[i].chrome;
    }
    if (controls[i].id == IdNewest) {
      newestChrome = controls[i].chrome;
    }
  }
  TEST_ASSERT_TRUE(progressChrome && newestChrome);

  UiSnapshot rows;
  rows.phase = UiPhase::Results;
  rows.rowPresent[0] = true;
  rows.rowSelected[0] = true;
  snprintf(rows.rowLabel[0], sizeof(rows.rowLabel[0]), "Office");
  snprintf(rows.rowDetail[0], sizeof(rows.rowDetail[0]), "SEC -40 dBm");
  count = collectUiControls(controls, 40, rows);
  bool sawRow = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdRow0) {
      sawRow = true;
      TEST_ASSERT_TRUE(controls[i].x == 6 && controls[i].y == 40 && controls[i].w == 210 && controls[i].h == 48);
      TEST_ASSERT_TRUE(controls[i].latched && !controls[i].chrome);
    }
  }
  TEST_ASSERT_TRUE(sawRow);

  UiSnapshot keys;
  keys.phase = UiPhase::Password;
  keys.shift = true;
  count = collectUiControls(controls, 40, keys);
  int keycaps = 0;
  bool shiftOk = false;
  bool pageOk = false;
  bool delOk = false;
  bool okPrimary = false;
  bool closeCancel = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id >= IdKeyBase && controls[i].id < IdRow0) {
      ++keycaps;
      TEST_ASSERT_TRUE(controls[i].label[0] != '\0' && controls[i].label[1] == '\0');
      TEST_ASSERT_TRUE(!controls[i].chrome);
    }
    if (controls[i].id == IdShift) {
      shiftOk = controls[i].latched && controls[i].secondary && !controls[i].cancel;
    }
    if (controls[i].id == IdPage) {
      pageOk = controls[i].secondary && !controls[i].cancel;
    }
    if (controls[i].id == IdDel) {
      delOk = controls[i].secondary && !controls[i].cancel;
    }
    if (controls[i].id == IdOk) {
      okPrimary = !controls[i].secondary && !controls[i].cancel && !controls[i].chrome;
    }
    if (controls[i].id == IdClose) {
      closeCancel = controls[i].cancel && !controls[i].secondary;
    }
  }
  TEST_ASSERT_TRUE(keycaps > 0 && shiftOk && pageOk && delOk && okPrimary && closeCancel);
  TEST_ASSERT_TRUE(IdShift == 10 && IdRow0 == 200);

  UiSnapshot menu;
  menu.phase = UiPhase::Settings;
  menu.settingsPage = SettingsPage::Menu;
  count = collectUiControls(controls, 40, menu);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::OpenService, -1, controls, count) == IdOpenService);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::OpenRange, -1, controls, count) == IdOpenRange);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SetProfile, 1, controls, count) == -1);
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdOpenService) {
      TEST_ASSERT_TRUE(controls[i].x == 8 && controls[i].y == 78 && controls[i].w == 206 && controls[i].h == 56);
      TEST_ASSERT_TRUE(controls[i].secondary && !controls[i].chrome);
    }
    if (controls[i].id == IdBack) {
      TEST_ASSERT_TRUE(controls[i].y == 410 && controls[i].w == 210 && controls[i].h == 40);
    }
  }

  UiSnapshot editor;
  editor.phase = UiPhase::Settings;
  editor.settingsPage = SettingsPage::Edit;
  count = collectUiControls(controls, 40, editor);
  bool sawDigit = false;
  bool sawLowBack = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdKeyBase) {
      sawDigit = controls[i].x == 8 && controls[i].y == 78 && controls[i].label[0] == '1';
    }
    if (controls[i].id == IdBack && controls[i].y == 410) {
      sawLowBack = true;
    }
  }
  TEST_ASSERT_TRUE(sawDigit && !sawLowBack);

  ScannerController scanner;
  AppView editView;
  editView.showingSettings = true;
  editView.settingsPage = SettingsPage::Edit;
  editView.resultsOpen = true;
  NavTrace nav{0, 0, 0, "home"};
  AppHooks hooks;
  hooks.closeResults = navClose;
  hooks.cancelPassword = navCancel;
  hooks.context = &nav;
  act(scanner, editView, true, IdBack, AppAction::Back, &hooks);
  TEST_ASSERT_TRUE(editView.showingSettings && editView.settingsPage == SettingsPage::Range);
  TEST_ASSERT_TRUE(nav.closes == 0 && nav.cancels == 0);

  const NetFacts wide = slash16Facts();
  AddressWindow automatic;
  CandidatePlan plan;
  RangePreview preview;
  previewAddressRange(wide, automatic, plan, preview);
  TEST_ASSERT_TRUE(preview.valid && preview.count == 255 && !preview.gatewayForced && preview.gatewayIncluded &&
                   preview.canNext && !preview.canPrev);
  TEST_ASSERT_TRUE(ipv4Equal(preview.start, ipv4(10, 1, 0, 1)));
  TEST_ASSERT_TRUE(ipv4Equal(preview.end, ipv4(10, 1, 0, 255)));
  TEST_ASSERT_TRUE(ipv4Equal(preview.nextOrigin, ipv4(10, 1, 1, 0)));
  TEST_ASSERT_TRUE(ipv4Equal(plan.address[0], ipv4(10, 1, 0, 1)));
  TEST_ASSERT_TRUE(planHas(plan, ipv4(10, 1, 5, 5)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 0, 9)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 1, 0)));

  AddressWindow custom;
  custom.mode = RangeMode::Custom;
  custom.useOrigin = true;
  custom.limit = 64;
  custom.origin = ipv4(10, 1, 2, 10);
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(preview.valid && preview.count == 64 && !preview.clamped && preview.canNext && preview.canPrev);
  TEST_ASSERT_TRUE(ipv4Equal(preview.start, ipv4(10, 1, 2, 10)));
  TEST_ASSERT_TRUE(ipv4Equal(preview.end, ipv4(10, 1, 2, 73)));
  TEST_ASSERT_FALSE(preview.gatewayIncluded);

  custom.limit = 128;
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(preview.valid && preview.count == 128 && preview.count <= kCandidateCap);
  custom.limit = 256;
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(preview.valid && preview.count == 256);

  custom.origin = ipv4(10, 2, 0, 1);
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(!preview.valid && strcmp(preview.reason, "outside") == 0 && plan.count == 0);
  custom.origin = ipv4(10, 1, 0, 0);
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(!preview.valid && strcmp(preview.reason, "outside") == 0);
  custom.origin = ipv4(10, 1, 255, 255);
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(!preview.valid && strcmp(preview.reason, "outside") == 0);

  custom.origin = ipv4(10, 1, 0, 9);
  custom.limit = 64;
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(preview.valid && ipv4Equal(preview.start, ipv4(10, 1, 0, 10)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 0, 9)));

  custom.origin = ipv4(10, 1, 255, 200);
  custom.limit = 256;
  previewAddressRange(wide, custom, plan, preview);
  TEST_ASSERT_TRUE(preview.valid && preview.clamped && preview.count == 55 && preview.count <= 256);
  TEST_ASSERT_TRUE(ipv4Equal(preview.end, ipv4(10, 1, 255, 254)));
  TEST_ASSERT_FALSE(planHas(plan, ipv4(10, 1, 255, 199)));

  const CandidatePlan slash24 = buildCandidatePlan(lan24());
  TEST_ASSERT_TRUE(slash24.valid && slash24.count == 253 && slash24.gatewayIncluded && !slash24.gatewayForced);
  const CandidatePlan slash28 =
      buildCandidatePlan(deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0)));
  TEST_ASSERT_TRUE(slash28.valid && slash28.count == 13 && slash28.gatewayIncluded);

  Ipv4 parsed;
  TEST_ASSERT_TRUE(parseIpv4("10.1.2.10", parsed) && ipv4Equal(parsed, ipv4(10, 1, 2, 10)));
  TEST_ASSERT_FALSE(parseIpv4("10.1.2", parsed));
  TEST_ASSERT_FALSE(parseIpv4("10.1.2.10 ", parsed));
  TEST_ASSERT_FALSE(parseIpv4("10.1.2.256", parsed));
  TEST_ASSERT_FALSE(addressLimitOk(0));
  TEST_ASSERT_FALSE(addressLimitOk(512));
  TEST_ASSERT_TRUE(addressLimitOk(64) && addressLimitOk(128) && addressLimitOk(256));

  scanner.armConnectedFacts(wide);
  TEST_ASSERT_TRUE(scanner.setLimit(64));
  TEST_ASSERT_TRUE(scanner.setCustomStart(ipv4(10, 1, 2, 10)));
  TEST_ASSERT_FALSE(scanner.setCustomStart(ipv4(10, 2, 0, 1)));
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.mode == RangeMode::Custom && ipv4Equal(preview.start, ipv4(10, 1, 2, 10)) && preview.limit == 64);
  TEST_ASSERT_TRUE(scanner.windowNext());
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.count <= 256 && ipv4Equal(preview.start, ipv4(10, 1, 2, 74)));
  TEST_ASSERT_TRUE(preview.end.octet[0] == 10 && preview.end.octet[1] == 1);
  TEST_ASSERT_TRUE(scanner.windowPrev());
  preview = scanner.preview();
  TEST_ASSERT_TRUE(ipv4Equal(preview.start, ipv4(10, 1, 2, 10)));
  scanner.setAutomatic();
  scanner.setLimit(256);
  TEST_ASSERT_TRUE(scanner.windowNext());
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.mode == RangeMode::Automatic && preview.count == 256 &&
                   ipv4Equal(preview.start, ipv4(10, 1, 1, 0)) && ipv4Equal(preview.end, ipv4(10, 1, 1, 255)) &&
                   !preview.gatewayIncluded);
  TEST_ASSERT_TRUE(scanner.windowPrev());
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.mode == RangeMode::Automatic && !preview.gatewayForced && preview.gatewayIncluded &&
                   preview.count == 255 && ipv4Equal(preview.start, ipv4(10, 1, 0, 1)));

  scanner.setCustomStart(ipv4(10, 1, 2, 10));
  scanner.setLimit(128);
  scanner.armConnectedFacts(wide);
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.mode == RangeMode::Custom && preview.limit == 128);
  scanner.armDisconnected();
  scanner.armConnectedFacts(wide);
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.mode == RangeMode::Automatic && preview.limit == 128);
  scanner.armConnectedFacts(lan24());
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.mode == RangeMode::Automatic && preview.limit == 128 && preview.count == 128 &&
                   preview.gatewayIncluded);
  TEST_ASSERT_TRUE(scanner.setLimit(256));
  preview = scanner.preview();
  TEST_ASSERT_TRUE(preview.count == 253 && preview.limit == 256 && preview.gatewayIncluded && !preview.gatewayForced);

  AppView rangeView;
  rangeView.showingSettings = true;
  rangeView.settingsPage = SettingsPage::Range;
  act(scanner, rangeView, true, IdCount64, AppAction::SetLimit, &hooks);
  TEST_ASSERT_TRUE(scanner.preview().limit == 64);
  act(scanner, rangeView, true, IdRangeAuto, AppAction::SetAutomatic, &hooks);
  TEST_ASSERT_TRUE(scanner.preview().mode == RangeMode::Automatic);

  RemoteWorld world;
  world.scanner = &scanner;
  world.view = &rangeView;
  RemoteSession session;
  char out[640];
  requireHello(session, world);
  int n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"custom\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"custom\",\"ok\":0}\n", out);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"custom\",\"ip\":\"10.9.9.9\"}", out,
                  static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"custom\",\"ok\":0}\n", out);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"count64\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"count64\",\"ok\":1}\n", out);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"custom\",\"ip\":\"192.168.0.40\"}", out,
                  static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"custom\",\"ok\":1}\n", out);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_TRUE(n > 0 && n < 576);
  TEST_ASSERT_TRUE(strstr(out, "\"range\":\"custom\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"rangeStart\":\"192.168.0.40\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "\"rangeLimit\":64") != nullptr);
  TEST_ASSERT_TRUE(strstr(out, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(out, "psk") == nullptr);
  TEST_ASSERT_TRUE(strstr(out, "passphrase") == nullptr);
  char line[240];
  AppState state;
  AppWifiView wifi;
  wifi.phase = "connected";
  wifi.ssid = "TFMiddle";
  fillAppState(state, rangeView, scanner, wifi, ServiceProfile::Common);
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "range") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "password") == nullptr);
  TEST_ASSERT_TRUE(strstr(line, "psk") == nullptr);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"automatic\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"automatic\",\"ok\":1}\n", out);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"count256\"}", out, static_cast<int>(sizeof(out)));
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"count256\",\"ok\":1}\n", out);
  TEST_ASSERT_TRUE(scanner.preview().mode == RangeMode::Automatic && scanner.preview().limit == 256);
  TEST_ASSERT_TRUE(scanner.preview().count <= 256);
}

bool lineInsideCardAndRegion(int y, int textH, int cardY, int cardH) {
  if (y < cardY || y + textH > cardY + cardH) {
    return false;
  }
  static const uint32_t bits[] = {UiRegionHeader, UiRegionWifiActions, UiRegionNetwork, UiRegionProgress,
                                   UiRegionLatest, UiRegionControls,    UiRegionFooter};
  int hits = 0;
  for (uint32_t bit : bits) {
    const UiRegionRect rect = uiRegionRect(bit);
    if (y >= rect.y && y + textH <= rect.y + rect.h) {
      ++hits;
    }
  }
  return hits == 1;
}

void test_host_card_lines_stay_in_one_region(void) {
  for (int row = 0; row < 6; ++row) {
    const int cardY = 40 + row * 52;
    for (int line = 0; line < 4; ++line) {
      int y = -1;
      TEST_ASSERT_TRUE(uiHostTextY(cardY, 48, line, 8, &y));
      TEST_ASSERT_TRUE(lineInsideCardAndRegion(y, 8, cardY, 48));
    }
  }
  int crossed = 66;
  TEST_ASSERT_FALSE(lineInsideCardAndRegion(crossed, 8, 40, 48));
}

void test_host_cards_do_not_share_fields(void) {
  const uint8_t macA[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t macB[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x66};
  ObservedHost rich;
  rich.ip = ipv4(10, 28, 100, 1);
  rich.hasMac = true;
  memcpy(rich.mac, macA, 6);
  memcpy(rich.name, "printer", 8);
  rich.nameSource = NameSource::ReverseDns;
  rich.ouiState = OuiState::Known;
  rich.manufacturer = "Vizio, Inc";
  ObservedHost sparse;
  sparse.ip = ipv4(10, 28, 100, 0);
  sparse.hasMac = false;
  sparse.ouiState = OuiState::None;
  ObservedHost middle;
  middle.ip = ipv4(10, 28, 100, 3);
  middle.hasMac = true;
  memcpy(middle.mac, macB, 6);
  middle.nameSource = NameSource::None;
  middle.ouiState = OuiState::Local;

  UiSnapshot page;
  page.phase = UiPhase::Hosts;
  const ObservedHost* hosts[6] = {&rich, &sparse, &middle, nullptr, nullptr, nullptr};
  for (int row = 0; row < 6; ++row) {
    if (hosts[row] == nullptr) {
      continue;
    }
    page.rowPresent[row] = true;
    formatIpv4(hosts[row]->ip, page.rowLabel[row], sizeof(page.rowLabel[row]));
    formatHostDetail(page.rowDetail[row], sizeof(page.rowDetail[row]), hosts[row]->nameSource, hosts[row]->name,
                     hosts[row]->hasMac, hosts[row]->mac);
    formatOuiLine(page.rowVendor[row], sizeof(page.rowVendor[row]), hosts[row]->ouiState, hosts[row]->manufacturer);
  }
  UiControl controls[8];
  const int count = collectUiControls(controls, 8, page);
  bool sawRich = false;
  bool sawSparse = false;
  bool sawMiddle = false;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.28.100.1") == 0) {
      sawRich = true;
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "Name: printer ") == controls[i].detail);
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "00:11:22:33:44:55") != nullptr);
      TEST_ASSERT_EQUAL_STRING("Vizio, Inc", controls[i].vendor);
    } else if (strcmp(controls[i].label, "10.28.100.0") == 0) {
      sawSparse = true;
      TEST_ASSERT_EQUAL_STRING("Name: unknown MAC unknown", controls[i].detail);
      TEST_ASSERT_EQUAL_STRING("", controls[i].vendor);
    } else if (strcmp(controls[i].label, "10.28.100.3") == 0) {
      sawMiddle = true;
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "Name: unknown ") == controls[i].detail);
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "02:11:22:33:44:66") != nullptr);
      TEST_ASSERT_EQUAL_STRING("local", controls[i].vendor);
      TEST_ASSERT_TRUE(strstr(controls[i].vendor, "Vizio") == nullptr);
    }
  }
  TEST_ASSERT_TRUE(sawRich && sawSparse && sawMiddle);

  UiSnapshot nextPage;
  nextPage.phase = UiPhase::Hosts;
  nextPage.rowPresent[0] = true;
  formatIpv4(middle.ip, nextPage.rowLabel[0], sizeof(nextPage.rowLabel[0]));
  formatHostDetail(nextPage.rowDetail[0], sizeof(nextPage.rowDetail[0]), middle.nameSource, middle.name, middle.hasMac,
                   middle.mac);
  formatOuiLine(nextPage.rowVendor[0], sizeof(nextPage.rowVendor[0]), middle.ouiState, middle.manufacturer);
  const int nextCount = collectUiControls(controls, 8, nextPage);
  bool sawPage = false;
  for (int i = 0; i < nextCount; ++i) {
    if (controls[i].id == IdRow0) {
      sawPage = true;
      TEST_ASSERT_EQUAL_STRING("10.28.100.3", controls[i].label);
      TEST_ASSERT_EQUAL_STRING("local", controls[i].vendor);
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "Vizio") == nullptr);
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "00:11:22:33:44:55") == nullptr);
    }
  }
  TEST_ASSERT_TRUE(sawPage);
}

void test_ptr_question_and_reply(void) {
  uint8_t query[128];
  size_t used = 0;
  TEST_ASSERT_TRUE(buildPtrQuestion(ipv4(10, 28, 100, 1), 0x1234, query, sizeof(query), &used));
  TEST_ASSERT_TRUE(used > 12);
  TEST_ASSERT_EQUAL_UINT(0x12, query[0]);
  TEST_ASSERT_EQUAL_UINT(0x34, query[1]);
  char name[128];
  PtrReply reply = PtrReply::Malformed;
  uint8_t nx[used];
  memcpy(nx, query, used);
  nx[2] = 0x81;
  nx[3] = 0x83;
  TEST_ASSERT_TRUE(parsePtrReply(nx, used, 0x1234, name, sizeof(name), &reply));
  TEST_ASSERT_TRUE(reply == PtrReply::NxDomain);

  uint8_t empty[used];
  memcpy(empty, query, used);
  empty[2] = 0x81;
  empty[3] = 0x80;
  TEST_ASSERT_TRUE(parsePtrReply(empty, used, 0x1234, name, sizeof(name), &reply));
  TEST_ASSERT_TRUE(reply == PtrReply::NoName);

  uint8_t answer[160];
  memset(answer, 0, sizeof(answer));
  memcpy(answer, query, used);
  answer[2] = 0x81;
  answer[3] = 0x80;
  answer[6] = 0x00;
  answer[7] = 0x01;
  size_t cursor = used;
  answer[cursor++] = 0xC0;
  answer[cursor++] = 0x0C;
  answer[cursor++] = 0x00;
  answer[cursor++] = 12;
  answer[cursor++] = 0x00;
  answer[cursor++] = 1;
  cursor += 4;
  answer[cursor++] = 0x00;
  answer[cursor++] = 9;
  answer[cursor++] = 7;
  memcpy(answer + cursor, "printer", 7);
  cursor += 7;
  answer[cursor++] = 0;
  TEST_ASSERT_TRUE(parsePtrReply(answer, cursor, 0x1234, name, sizeof(name), &reply));
  TEST_ASSERT_TRUE(reply == PtrReply::Resolved);
  TEST_ASSERT_EQUAL_STRING("printer", name);
  TEST_ASSERT_EQUAL_STRING("resolved", ptrReplyLabel(reply));
}

class ScriptedConnect : public ServiceConnectBackend {
 public:
  ServiceConnectStatus script[64] = {};
  int scriptCount = 0;
  int cursor = 0;
  int starts = 0;
  int cancels = 0;
  bool pending = false;
  ServiceConnectStatus held = ServiceConnectStatus::Pending;
  uint16_t ports[64] = {};
  Ipv4 ips[64] = {};

  void start(const Ipv4& ip, uint16_t port, uint32_t nowMs, uint32_t timeoutMs) override {
    (void)nowMs;
    (void)timeoutMs;
    if (starts < 64) {
      ports[starts] = port;
      ips[starts] = ip;
    }
    ++starts;
    pending = true;
    held = cursor < scriptCount ? script[cursor++] : ServiceConnectStatus::Error;
  }

  ServiceConnectStatus poll(uint32_t nowMs) override {
    (void)nowMs;
    if (!pending) {
      return ServiceConnectStatus::Error;
    }
    if (held == ServiceConnectStatus::Pending) {
      return ServiceConnectStatus::Pending;
    }
    pending = false;
    return held;
  }

  void cancel() override {
    ++cancels;
    pending = false;
    held = ServiceConnectStatus::Pending;
  }
};

static void fillMac(HostInventory& inventory, uint8_t last, uint8_t mac0) {
  const uint8_t mac[6] = {mac0, 0x11, 0x22, 0x33, 0x44, last};
  inventory.observe(ipv4(10, 0, 0, last), EvidenceRank::Neighbor, true, mac, false, 0, 1, "arp");
}

static void test_service_profiles_and_scan(void) {
  TEST_ASSERT_EQUAL_UINT8(3, serviceProfilePortCount(ServiceProfile::Basic));
  TEST_ASSERT_EQUAL_UINT8(9, serviceProfilePortCount(ServiceProfile::Common));
  TEST_ASSERT_EQUAL_UINT8(20, serviceProfilePortCount(ServiceProfile::Detailed));
  TEST_ASSERT_EQUAL_UINT16(22, serviceProfilePort(ServiceProfile::Basic, 0));
  TEST_ASSERT_EQUAL_UINT16(80, serviceProfilePort(ServiceProfile::Basic, 1));
  TEST_ASSERT_EQUAL_UINT16(443, serviceProfilePort(ServiceProfile::Basic, 2));
  TEST_ASSERT_EQUAL_UINT16(0, serviceProfilePort(ServiceProfile::Basic, 3));
  const uint16_t common[] = {22, 80, 443, 445, 548, 631, 8080, 8443, 9100};
  for (uint8_t i = 0; i < 9; ++i) {
    TEST_ASSERT_EQUAL_UINT16(common[i], serviceProfilePort(ServiceProfile::Common, i));
  }
  const uint16_t extra[] = {21, 23, 25, 53, 110, 143, 587, 993, 995, 1883, 8883};
  for (uint8_t i = 0; i < 11; ++i) {
    TEST_ASSERT_EQUAL_UINT16(extra[i], serviceProfilePort(ServiceProfile::Detailed, static_cast<uint8_t>(9 + i)));
  }
  TEST_ASSERT_EQUAL_UINT32(200, serviceProfileTimeoutMs(ServiceProfile::Basic));
  TEST_ASSERT_EQUAL_UINT32(250, serviceProfileTimeoutMs(ServiceProfile::Common));
  TEST_ASSERT_EQUAL_UINT32(300, serviceProfileTimeoutMs(ServiceProfile::Detailed));
  TEST_ASSERT_EQUAL_STRING("ssh", serviceProfilePortFamily(ServiceProfile::Basic, 0));
  TEST_ASSERT_TRUE(serviceProfilePortFamily(ServiceProfile::Basic, 3) == nullptr);

  HostInventory inventory;
  fillMac(inventory, 1, 0x00);
  inventory.observe(ipv4(10, 0, 0, 2), EvidenceRank::Answered, false, nullptr, false, 0, 1, "arp");
  fillMac(inventory, 3, 0x00);
  ScriptedConnect backend;
  backend.script[0] = ServiceConnectStatus::Open;
  backend.script[1] = ServiceConnectStatus::Closed;
  backend.script[2] = ServiceConnectStatus::Timeout;
  backend.script[3] = ServiceConnectStatus::Error;
  backend.script[4] = ServiceConnectStatus::Open;
  backend.script[5] = ServiceConnectStatus::Pending;
  backend.scriptCount = 6;
  ServiceScan scan;
  scan.setBackend(&backend);
  scan.setProfile(ServiceProfile::Basic);
  TEST_ASSERT_TRUE(scan.arm(inventory, 0));
  TEST_ASSERT_EQUAL_UINT16(2, scan.targetCount());
  TEST_ASSERT_EQUAL_UINT16(6, scan.planned());
  for (int step = 0; step < 5; ++step) {
    scan.loop(static_cast<uint32_t>(step), inventory);
  }
  const uint16_t startedAtPause = scan.startedCount();
  const uint16_t doneAtPause = scan.completed();
  scan.pause();
  scan.loop(20, inventory);
  TEST_ASSERT_TRUE(scan.paused());
  TEST_ASSERT_EQUAL_UINT16(startedAtPause, scan.startedCount());
  TEST_ASSERT_EQUAL_UINT16(doneAtPause, scan.completed());
  scan.resume();
  scan.loop(21, inventory);
  TEST_ASSERT_EQUAL_UINT16(5, scan.completed());
  TEST_ASSERT_EQUAL_UINT16(6, scan.startedCount());
  scan.stop();
  scan.loop(22, inventory);
  TEST_ASSERT_TRUE(scan.run() == ServiceRun::Stopped);
  TEST_ASSERT_EQUAL_UINT16(5, scan.completed());
  TEST_ASSERT_EQUAL_UINT16(6, scan.startedCount());
  TEST_ASSERT_EQUAL_UINT16(1, scan.cancelledCount());
  TEST_ASSERT_EQUAL_INT(1, backend.cancels);
  TEST_ASSERT_EQUAL_UINT16(22, backend.ports[0]);
  TEST_ASSERT_EQUAL_UINT16(80, backend.ports[1]);
  TEST_ASSERT_EQUAL_UINT16(443, backend.ports[2]);
  TEST_ASSERT_EQUAL_UINT8(1, backend.ips[0].octet[3]);
  TEST_ASSERT_EQUAL_UINT8(3, backend.ips[3].octet[3]);
  TEST_ASSERT_TRUE(scan.resultAt(1)->tested == 0);
  TEST_ASSERT_TRUE(scan.resultAt(0)->state[0] == ServiceProbeClass::Open);
  TEST_ASSERT_TRUE(scan.resultAt(0)->state[1] == ServiceProbeClass::Closed);
  TEST_ASSERT_TRUE(scan.resultAt(0)->state[2] == ServiceProbeClass::Timeout);
  TEST_ASSERT_TRUE(scan.resultAt(2)->state[0] == ServiceProbeClass::Error);
  TEST_ASSERT_EQUAL_UINT16(2, scan.openPorts());
  TEST_ASSERT_EQUAL_UINT16(1, scan.closedCount());
  TEST_ASSERT_EQUAL_UINT16(1, scan.timeoutCount());
  TEST_ASSERT_EQUAL_UINT16(1, scan.errorCount());
  char field[160];
  TEST_ASSERT_TRUE(formatServiceField(field, sizeof(field), scan.profile(), scan.resultAt(0)));
  TEST_ASSERT_EQUAL_STRING("22:o|80:c|443:t", field);
  InventoryRow row;
  row.ip[0] = '1';
  snprintf(row.services, sizeof(row.services), "%s", field);
  snprintf(row.manufacturer, sizeof(row.manufacturer), "Acme, Widgets");
  char line[512];
  TEST_ASSERT_TRUE(formatInventoryRowLine(line, static_cast<int>(sizeof(line)), row));
  TEST_ASSERT_TRUE(strstr(line, "\"Acme, Widgets\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(line, "22:o|80:c|443:t") != nullptr);
  ScannerController controller;
  controller.bindServiceScan(&scan);
  scan.reset();
  backend = ScriptedConnect();
  backend.script[0] = ServiceConnectStatus::Pending;
  backend.scriptCount = 1;
  scan.setBackend(&backend);
  scan.setProfile(ServiceProfile::Basic);
  TEST_ASSERT_TRUE(scan.arm(inventory, 0));
  scan.loop(30, inventory);
  TEST_ASSERT_TRUE(scan.running());
  controller.pause();
  TEST_ASSERT_TRUE(scan.paused());
  controller.resume();
  TEST_ASSERT_TRUE(scan.running());
  TEST_ASSERT_EQUAL_INT(1, backend.starts);
  controller.stop();
  TEST_ASSERT_TRUE(scan.run() == ServiceRun::Stopped);
  TEST_ASSERT_EQUAL_INT(1, backend.cancels);
  controller.reset();
  TEST_ASSERT_TRUE(scan.run() == ServiceRun::Idle);
  TEST_ASSERT_EQUAL_UINT16(0, scan.planned());
  TEST_ASSERT_EQUAL_UINT8(0, scan.resultAt(0)->tested);

  ScriptedConnect again;
  for (int i = 0; i < 40; ++i) {
    again.script[i] = ServiceConnectStatus::Closed;
  }
  again.scriptCount = 40;
  ServiceScan detailed;
  detailed.setBackend(&again);
  detailed.setProfile(ServiceProfile::Detailed);
  HostInventory one;
  fillMac(one, 9, 0x02);
  TEST_ASSERT_TRUE(detailed.arm(one, 0));
  TEST_ASSERT_EQUAL_UINT16(20, detailed.planned());
  for (int step = 0; step < 40 && detailed.running(); ++step) {
    detailed.loop(static_cast<uint32_t>(step), one);
  }
  TEST_ASSERT_TRUE(detailed.run() == ServiceRun::Complete);
  TEST_ASSERT_EQUAL_UINT16(20, detailed.completed());
  TEST_ASSERT_EQUAL_UINT16(20, detailed.startedCount());
  TEST_ASSERT_EQUAL_UINT16(8883, again.ports[19]);
  TEST_ASSERT_EQUAL_UINT16(0, again.ports[20]);

  char label[22];
  char detail[22];
  TEST_ASSERT_TRUE(formatServiceProgressLabel(label, sizeof(label), 5120, 5120));
  TEST_ASSERT_TRUE(formatServiceProgressDetail(detail, sizeof(detail), "detailed", 5120));
  TEST_ASSERT_TRUE(strlen(label) < sizeof(label));
  TEST_ASSERT_TRUE(strlen(detail) < sizeof(detail));

  UiSnapshot hosts = homeSnapshot();
  hosts.phase = UiPhase::Hosts;
  hosts.rowPresent[0] = true;
  snprintf(hosts.rowLabel[0], sizeof(hosts.rowLabel[0]), "10.0.0.1");
  snprintf(hosts.rowNote[0], sizeof(hosts.rowNote[0]), "open 2");
  UiControl controls[8];
  const int count = collectUiControls(controls, 8, hosts);
  TEST_ASSERT_TRUE(count >= 1);
  TEST_ASSERT_EQUAL_STRING("open 2", controls[0].note);

  ServiceHostResult full;
  full.tested = 20;
  full.openCount = 20;
  for (uint8_t i = 0; i < 20; ++i) {
    full.state[i] = ServiceProbeClass::Open;
  }
  char ports[160];
  TEST_ASSERT_TRUE(formatServiceWirePorts(ports, sizeof(ports), ServiceProfile::Detailed, &full));
  struct WireHolder {
    char ports[160];
  } holder;
  snprintf(holder.ports, sizeof(holder.ports), "%s", ports);
  RemoteServices services;
  services.rowCount = [](void* context) -> int {
    (void)context;
    return 1;
  };
  services.serviceAt = [](void* context, int index, ServiceWireRow* out) -> bool {
    auto* held = static_cast<WireHolder*>(context);
    if (out == nullptr || index != 0 || held == nullptr) {
      return false;
    }
    *out = ServiceWireRow();
    snprintf(out->ip, sizeof(out->ip), "255.255.255.255");
    snprintf(out->ports, sizeof(out->ports), "%s", held->ports);
    out->openCount = 20;
    return true;
  };
  services.rowAt = [](void* context, int index, InventoryRow* out) -> bool {
    (void)context;
    if (out == nullptr || index != 0) {
      return false;
    }
    *out = InventoryRow();
    snprintf(out->ip, sizeof(out->ip), "10.0.0.1");
    return true;
  };
  services.context = &holder;
  RemoteSession session;
  char frame[576];
  int n = remoteSubmit(&session, "@R1 {\"v\":1,\"op\":\"HELLO\"}", frame, static_cast<int>(sizeof(frame)), &services);
  TEST_ASSERT_TRUE(strstr(frame, "HELLO_ACK") != nullptr);
  n = remoteSubmit(&session, "@R1 {\"v\":1,\"op\":\"GET_SERVICES\"}", frame, static_cast<int>(sizeof(frame)), &services);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(strstr(frame, "\"op\":\"SERVICE_ROW\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(frame, ports) != nullptr);
  TEST_ASSERT_TRUE(strchr(frame, '\n') != nullptr);
  const size_t beforeNewline = static_cast<size_t>(strchr(frame, '\n') - frame);
  TEST_ASSERT_TRUE(beforeNewline <= 320);
  n = remotePull(&session, frame, static_cast<int>(sizeof(frame)), &services);
  TEST_ASSERT_TRUE(strstr(frame, "\"op\":\"SERVICE_END\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(frame, "\"count\":1") != nullptr);
  n = remoteSubmit(&session, "@R1 {\"v\":1,\"op\":\"GET_RESULTS\"}", frame, static_cast<int>(sizeof(frame)), &services);
  TEST_ASSERT_TRUE(strstr(frame, "\"op\":\"RESULT_ROW\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(frame, "ports") == nullptr);
  remotePull(&session, frame, static_cast<int>(sizeof(frame)), &services);

  RemoteWorld world;
  world.haveState = true;
  world.state.screen = AppScreen::Settings;
  snprintf(world.state.wifiPhase, sizeof(world.state.wifiPhase), "connecting");
  memset(world.state.ssid, 'S', 32);
  world.state.ssid[32] = '\0';
  snprintf(world.state.scan, sizeof(world.state.scan), "SCANNING");
  world.state.processed = 256;
  world.state.candidates = 256;
  world.state.observed = 256;
  snprintf(world.state.current, sizeof(world.state.current), "255.255.255.255");
  snprintf(world.state.last, sizeof(world.state.last), "255.255.255.255");
  snprintf(world.state.newest, sizeof(world.state.newest), "255.255.255.255");
  world.state.elapsedMs = 2147483647u;
  world.state.hostsOpen = true;
  world.state.page = 42;
  world.state.canStart = true;
  world.state.canPause = true;
  world.state.canResume = true;
  snprintf(world.state.profile, sizeof(world.state.profile), "detailed");
  snprintf(world.state.rangeMode, sizeof(world.state.rangeMode), "automatic");
  snprintf(world.state.rangeStart, sizeof(world.state.rangeStart), "255.255.255.255");
  snprintf(world.state.rangeEnd, sizeof(world.state.rangeEnd), "255.255.255.255");
  world.state.rangeLimit = 256;
  snprintf(world.state.ack, sizeof(world.state.ack), "windownext");
  snprintf(world.state.jobPhase, sizeof(world.state.jobPhase), "svc");
  world.state.svcPlan = 5120;
  world.state.svcDone = 5120;
  world.state.svcOpenHosts = 256;
  world.state.svcOpen = 5120;
  RemoteSession stateSession;
  n = submitWorld(stateSession, world, "@R1 {\"v\":1,\"op\":\"HELLO\"}", frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(strstr(frame, "HELLO_ACK") != nullptr);
  n = submitWorld(stateSession, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_TRUE(strstr(frame, "\"reason\":\"state\"") == nullptr);
  TEST_ASSERT_TRUE(strstr(frame, "\"svc\":\"s/5120/5120/256/5120\"") != nullptr);
  TEST_ASSERT_TRUE(n < 512);
  TEST_ASSERT_TRUE(strstr(frame, "password") == nullptr);
  AppState diagnosticState;
  snprintf(diagnosticState.wifiPhase, sizeof(diagnosticState.wifiPhase), "connected");
  snprintf(diagnosticState.ssid, sizeof(diagnosticState.ssid), "TFMiddle");
  snprintf(diagnosticState.scan, sizeof(diagnosticState.scan), "COMPLETE");
  snprintf(diagnosticState.jobPhase, sizeof(diagnosticState.jobPhase), "svc");
  diagnosticState.svcPlan = 27;
  diagnosticState.svcDone = 27;
  diagnosticState.svcOpen = 2;
  char diagnostic[240];
  TEST_ASSERT_TRUE(formatAppStateLine(diagnostic, static_cast<int>(sizeof(diagnostic)), diagnosticState) > 0);
  TEST_ASSERT_TRUE(strstr(diagnostic, "svcPlan") == nullptr);
  TEST_ASSERT_TRUE(strstr(diagnostic, "jobPhase") == nullptr);
}

static const ServiceHostResult* uxResultAt(void* context, uint16_t index) {
  auto* rows = static_cast<ServiceHostResult*>(context);
  if (rows == nullptr || index >= 4) {
    return nullptr;
  }
  return &rows[index];
}

void test_service_result_ux(void) {
  ServiceHostResult rows[4];
  rows[0].tested = 3;
  rows[0].openCount = 2;
  rows[0].state[0] = ServiceProbeClass::Open;
  rows[0].state[1] = ServiceProbeClass::Open;
  rows[0].state[2] = ServiceProbeClass::Closed;
  rows[1].tested = 3;
  rows[1].state[0] = ServiceProbeClass::Closed;
  rows[1].state[1] = ServiceProbeClass::Closed;
  rows[1].state[2] = ServiceProbeClass::Closed;
  rows[2].tested = 0;
  rows[3].tested = 3;
  rows[3].state[0] = ServiceProbeClass::Timeout;
  rows[3].state[1] = ServiceProbeClass::Error;
  rows[3].state[2] = ServiceProbeClass::Closed;
  const Ipv4 ips[4] = {ipv4(10, 0, 0, 30), ipv4(10, 0, 0, 2), ipv4(10, 0, 0, 10), ipv4(10, 0, 0, 5)};
  const Ipv4 saved[4] = {ips[0], ips[1], ips[2], ips[3]};

  char summary[22];
  char csv[64];
  TEST_ASSERT_TRUE(formatServiceSummary(summary, sizeof(summary), ServiceProfile::Basic, &rows[0]));
  TEST_ASSERT_EQUAL_STRING("Open: 22 SSH, 80 HTTP", summary);
  TEST_ASSERT_TRUE(strlen(summary) < sizeof(summary));
  TEST_ASSERT_TRUE(strstr(summary, "password") == nullptr);
  TEST_ASSERT_TRUE(formatServiceField(csv, sizeof(csv), ServiceProfile::Basic, &rows[0]));
  TEST_ASSERT_EQUAL_STRING("22:o|80:o|443:c", csv);
  TEST_ASSERT_TRUE(formatServiceSummary(summary, sizeof(summary), ServiceProfile::Basic, &rows[1]));
  TEST_ASSERT_EQUAL_STRING("Open: none", summary);
  TEST_ASSERT_TRUE(formatServiceSummary(summary, sizeof(summary), ServiceProfile::Basic, &rows[2]));
  TEST_ASSERT_EQUAL_STRING("Not scanned", summary);
  TEST_ASSERT_TRUE(formatServiceSummary(summary, sizeof(summary), ServiceProfile::Basic, nullptr));
  TEST_ASSERT_EQUAL_STRING("Not scanned", summary);
  TEST_ASSERT_EQUAL_STRING("TIMEOUT", serviceStateWord(ServiceProbeClass::Timeout));
  TEST_ASSERT_TRUE(strcmp(serviceStateWord(ServiceProbeClass::Timeout), "CLOSED") != 0);
  TEST_ASSERT_EQUAL_STRING("ERROR", serviceStateWord(ServiceProbeClass::Error));
  TEST_ASSERT_EQUAL_STRING("OPEN", serviceStateWord(ServiceProbeClass::Open));
  TEST_ASSERT_EQUAL_STRING("CLOSED", serviceStateWord(ServiceProbeClass::Closed));
  TEST_ASSERT_TRUE(serviceHostHasOpen(&rows[0]));
  TEST_ASSERT_FALSE(serviceHostHasOpen(&rows[1]));
  TEST_ASSERT_FALSE(serviceHostHasOpen(&rows[2]));
  TEST_ASSERT_FALSE(serviceHostHasOpen(&rows[3]));

  char tight[16];
  TEST_ASSERT_TRUE(formatServiceSummary(tight, sizeof(tight), ServiceProfile::Basic, &rows[0]));
  TEST_ASSERT_EQUAL_STRING("Open: 22 SSH +1", tight);
  ServiceHostResult alt;
  alt.tested = 9;
  alt.openCount = 1;
  alt.state[6] = ServiceProbeClass::Open;
  TEST_ASSERT_TRUE(formatServiceSummary(summary, sizeof(summary), ServiceProfile::Common, &alt));
  TEST_ASSERT_EQUAL_STRING("Open: 8080 HTTP-ALT", summary);

  uint16_t order[4];
  TEST_ASSERT_EQUAL_INT(4, buildServiceHostView(order, 4, 4, ips, uxResultAt, rows, false));
  TEST_ASSERT_EQUAL_UINT16(1, order[0]);
  TEST_ASSERT_EQUAL_UINT16(3, order[1]);
  TEST_ASSERT_EQUAL_UINT16(2, order[2]);
  TEST_ASSERT_EQUAL_UINT16(0, order[3]);
  TEST_ASSERT_TRUE(ipv4Equal(ips[0], saved[0]) && ipv4Equal(ips[3], saved[3]));
  TEST_ASSERT_EQUAL_INT(1, buildServiceHostView(order, 4, 4, ips, uxResultAt, rows, true));
  TEST_ASSERT_EQUAL_UINT16(0, order[0]);
  ServiceHostResult quiet[4];
  for (int i = 0; i < 4; ++i) {
    quiet[i].tested = 1;
    quiet[i].state[0] = ServiceProbeClass::Closed;
  }
  TEST_ASSERT_EQUAL_INT(0, buildServiceHostView(order, 4, 4, ips, uxResultAt, quiet, true));
  TEST_ASSERT_EQUAL_INT(4, buildServiceHostView(order, 4, 4, ips, uxResultAt, quiet, false));

  char label[22];
  TEST_ASSERT_TRUE(formatServicePortLabel(label, sizeof(label), serviceProfilePort(ServiceProfile::Detailed, 0),
                                          serviceProfilePortFamily(ServiceProfile::Detailed, 0)));
  TEST_ASSERT_EQUAL_STRING("22 SSH", label);
  TEST_ASSERT_TRUE(formatServicePortLabel(label, sizeof(label), serviceProfilePort(ServiceProfile::Detailed, 10),
                                          serviceProfilePortFamily(ServiceProfile::Detailed, 10)));
  TEST_ASSERT_EQUAL_STRING("23 TELNET", label);
  TEST_ASSERT_TRUE(formatServicePortLabel(label, sizeof(label), serviceProfilePort(ServiceProfile::Detailed, 19),
                                          serviceProfilePortFamily(ServiceProfile::Detailed, 19)));
  TEST_ASSERT_EQUAL_STRING("8883 MQTTS", label);
  TEST_ASSERT_EQUAL_INT(0, 0 * 6 + 0);
  TEST_ASSERT_EQUAL_INT(10, 1 * 6 + 4);
  TEST_ASSERT_EQUAL_INT(19, 3 * 6 + 1);

  UiSnapshot cards;
  cards.phase = UiPhase::Hosts;
  cards.rowPresent[0] = true;
  cards.rowPresent[1] = true;
  snprintf(cards.rowLabel[0], sizeof(cards.rowLabel[0]), "10.0.0.30");
  snprintf(cards.rowLabel[1], sizeof(cards.rowLabel[1]), "10.0.0.2");
  snprintf(cards.rowDetail[0], sizeof(cards.rowDetail[0]), "Name: printer 02:00:00:00:00:01");
  snprintf(cards.rowDetail[1], sizeof(cards.rowDetail[1]), "Name: unknown MAC unknown");
  snprintf(cards.rowNote[0], sizeof(cards.rowNote[0]), "Open: 22 SSH");
  snprintf(cards.rowNote[1], sizeof(cards.rowNote[1]), "Open: none");
  UiControl controls[16];
  int count = collectUiControls(controls, 16, cards);
  bool sawOpen = false;
  bool sawNone = false;
  bool sawFilter = false;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.0.0.30") == 0) {
      sawOpen = true;
      TEST_ASSERT_EQUAL_STRING("Open: 22 SSH", controls[i].note);
      TEST_ASSERT_TRUE(strstr(controls[i].note, "none") == nullptr);
      TEST_ASSERT_TRUE(strstr(controls[i].detail, "printer") != nullptr);
      TEST_ASSERT_TRUE(controls[i].y == 40);
    } else if (strcmp(controls[i].label, "10.0.0.2") == 0) {
      sawNone = true;
      TEST_ASSERT_EQUAL_STRING("Open: none", controls[i].note);
      TEST_ASSERT_TRUE(strstr(controls[i].note, "22 SSH") == nullptr);
    } else if (controls[i].id == IdOpenOnly) {
      sawFilter = true;
      TEST_ASSERT_EQUAL_STRING("Open only", controls[i].label);
      TEST_ASSERT_TRUE(controls[i].x == 130 && controls[i].y == 6 && controls[i].w == 84 && controls[i].h == 26);
    }
  }
  TEST_ASSERT_TRUE(sawOpen && sawNone && sawFilter);
  cards.openOnly = true;
  cards.hostDetail = true;
  cards.rowPresent[1] = false;
  snprintf(cards.rowLabel[0], sizeof(cards.rowLabel[0]), "22 SSH");
  snprintf(cards.rowDetail[0], sizeof(cards.rowDetail[0]), "OPEN");
  count = collectUiControls(controls, 16, cards);
  bool sawDetail = false;
  bool sawAll = false;
  bool sawOpenButton = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdRow0) {
      sawDetail = true;
      TEST_ASSERT_EQUAL_STRING("22 SSH", controls[i].label);
      TEST_ASSERT_EQUAL_STRING("OPEN", controls[i].detail);
      TEST_ASSERT_TRUE(controls[i].y == 72);
    }
    if (controls[i].id == IdAllHosts) {
      sawAll = true;
    }
    if (controls[i].id == IdOpenOnly) {
      sawOpenButton = true;
    }
  }
  TEST_ASSERT_TRUE(sawDetail && !sawAll && !sawOpenButton);

  ScannerController scanner;
  AppView view;
  view.showingHosts = true;
  view.page = 2;
  view.detailIndex = 4;
  view.detailPage = 1;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::SetFilterOpen, view, scanner, nullptr));
  TEST_ASSERT_TRUE(view.openOnly && view.page == 0 && view.detailIndex == -1 && view.showingHosts);
  TEST_ASSERT_TRUE(applyAppAction(AppAction::SetFilterAll, view, scanner, nullptr));
  TEST_ASSERT_FALSE(view.openOnly);
  view.openOnly = true;
  view.page = 2;
  view.detailIndex = 7;
  view.detailCount = 20;
  view.detailPage = 0;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::NextPage, view, scanner, nullptr));
  TEST_ASSERT_EQUAL_INT(1, view.detailPage);
  TEST_ASSERT_EQUAL_INT(2, view.page);
  TEST_ASSERT_TRUE(applyAppAction(AppAction::NextPage, view, scanner, nullptr));
  TEST_ASSERT_TRUE(applyAppAction(AppAction::NextPage, view, scanner, nullptr));
  TEST_ASSERT_EQUAL_INT(3, view.detailPage);
  TEST_ASSERT_EQUAL_INT(2, view.page);
  TEST_ASSERT_TRUE(applyAppAction(AppAction::NextPage, view, scanner, nullptr));
  TEST_ASSERT_EQUAL_INT(3, view.detailPage);
  TEST_ASSERT_TRUE(applyAppAction(AppAction::Back, view, scanner, nullptr));
  TEST_ASSERT_TRUE(view.showingHosts && view.detailIndex == -1 && view.openOnly);
  TEST_ASSERT_EQUAL_INT(2, view.page);
  view.detailIndex = -1;
  view.selectedInventory = 3;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::SelectRow, view, scanner, nullptr));
  TEST_ASSERT_EQUAL_INT(3, view.detailIndex);
  const int kept = view.detailIndex;
  view.selectedInventory = 1;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::SelectRow, view, scanner, nullptr));
  TEST_ASSERT_EQUAL_INT(kept, view.detailIndex);
  TEST_ASSERT_TRUE(applyAppAction(AppAction::ResetScan, view, scanner, nullptr));
  TEST_ASSERT_FALSE(view.openOnly);
  TEST_ASSERT_EQUAL_INT(-1, view.detailIndex);
  TEST_ASSERT_EQUAL_INT(0, view.page);

  AppView idleView;
  idleView.openOnly = true;
  idleView.page = 4;
  idleView.detailIndex = 3;
  ScannerController unarmed;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::StartScan, idleView, unarmed, nullptr));
  TEST_ASSERT_TRUE(idleView.openOnly && idleView.page == 4 && idleView.detailIndex == 3);
  gScanNow = 8000;
  FakeDiscoveryBackend backend;
  armReady(scanner, backend, lan24(), 60000);
  AppView started;
  started.openOnly = true;
  started.page = 4;
  started.detailIndex = 3;
  started.detailPage = 2;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::StartScan, started, scanner, nullptr));
  TEST_ASSERT_TRUE(scanner.state() == ScanState::Starting);
  TEST_ASSERT_TRUE(started.openOnly);
  TEST_ASSERT_EQUAL_INT(0, started.page);
  TEST_ASSERT_EQUAL_INT(-1, started.detailIndex);

  view = AppView();
  view.showingHosts = false;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::SetFilterOpen, view, scanner, nullptr));
  TEST_ASSERT_FALSE(view.openOnly);
  view.showingHosts = true;
  view.observedCount = 13;
  TEST_ASSERT_TRUE(applyAppAction(AppAction::NextPage, view, scanner, nullptr));
  TEST_ASSERT_EQUAL_INT(1, view.page);

  AppState state;
  AppWifiView wifi;
  wifi.phase = "connected";
  wifi.ssid = "TFMiddle";
  view.openOnly = true;
  view.showingHosts = true;
  fillAppState(state, view, scanner, wifi);
  char line[240];
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "hosts=2") != nullptr);
  TEST_ASSERT_TRUE(strstr(line, "password") == nullptr);
  view.openOnly = false;
  fillAppState(state, view, scanner, wifi);
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "hosts=1") != nullptr);
  view.showingHosts = false;
  view.openOnly = true;
  fillAppState(state, view, scanner, wifi);
  TEST_ASSERT_TRUE(formatAppStateLine(line, static_cast<int>(sizeof(line)), state) > 0);
  TEST_ASSERT_TRUE(strstr(line, "hosts=0") != nullptr);

  RemoteWorld world;
  world.scanner = &scanner;
  world.view = &view;
  view.showingHosts = true;
  view.openOnly = true;
  view.page = 1;
  world.rowsCount = 2;
  snprintf(world.rows[0].ip, sizeof(world.rows[0].ip), "10.0.0.30");
  snprintf(world.rows[1].ip, sizeof(world.rows[1].ip), "10.0.0.2");
  RemoteSession session;
  requireHello(session, world);
  char frame[640];
  int n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"ACTION\",\"name\":\"openonly\"}", frame,
                      static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("@R1 {\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"openonly\",\"ok\":1}\n", frame);
  TEST_ASSERT_TRUE(view.openOnly && view.page == 0);
  n = submitWorld(session, world, "@R1 {\"v\":1,\"op\":\"GET_RESULTS\"}", frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(strstr(frame, "10.0.0.30") != nullptr);
  n = pullWorld(session, world, frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(strstr(frame, "10.0.0.2") != nullptr);
  n = pullWorld(session, world, frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(strstr(frame, "\"op\":\"RESULT_END\"") != nullptr);
  TEST_ASSERT_TRUE(strstr(frame, "\"count\":2") != nullptr);

  world.haveState = true;
  world.state.screen = AppScreen::Hosts;
  snprintf(world.state.wifiPhase, sizeof(world.state.wifiPhase), "connecting");
  memset(world.state.ssid, 'S', 32);
  world.state.ssid[32] = '\0';
  snprintf(world.state.scan, sizeof(world.state.scan), "SCANNING");
  world.state.processed = 256;
  world.state.candidates = 256;
  world.state.observed = 256;
  snprintf(world.state.current, sizeof(world.state.current), "255.255.255.255");
  snprintf(world.state.last, sizeof(world.state.last), "255.255.255.255");
  snprintf(world.state.newest, sizeof(world.state.newest), "255.255.255.255");
  world.state.elapsedMs = 2147483647u;
  world.state.hostsOpen = true;
  world.state.openOnly = false;
  world.state.page = 42;
  world.state.canStart = true;
  world.state.canPause = true;
  world.state.canResume = true;
  snprintf(world.state.profile, sizeof(world.state.profile), "detailed");
  snprintf(world.state.rangeMode, sizeof(world.state.rangeMode), "automatic");
  snprintf(world.state.rangeStart, sizeof(world.state.rangeStart), "255.255.255.255");
  snprintf(world.state.rangeEnd, sizeof(world.state.rangeEnd), "255.255.255.255");
  world.state.rangeLimit = 256;
  snprintf(world.state.ack, sizeof(world.state.ack), "windownext");
  snprintf(world.state.jobPhase, sizeof(world.state.jobPhase), "svc");
  world.state.svcPlan = 5120;
  world.state.svcDone = 5120;
  world.state.svcOpenHosts = 256;
  world.state.svcOpen = 5120;
  RemoteSession stateSession;
  n = submitWorld(stateSession, world, "@R1 {\"v\":1,\"op\":\"HELLO\"}", frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(strstr(frame, "HELLO_ACK") != nullptr);
  n = submitWorld(stateSession, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", frame, static_cast<int>(sizeof(frame)));
  const int allFrame = n;
  TEST_ASSERT_TRUE(strstr(frame, "\"hosts\":1") != nullptr);
  TEST_ASSERT_TRUE(strstr(frame, "\"svc\":\"s/5120/5120/256/5120\"") != nullptr);
  TEST_ASSERT_TRUE(n > 0 && n < 512);
  world.state.openOnly = true;
  n = submitWorld(stateSession, world, "@R1 {\"v\":1,\"op\":\"GET_STATE\"}", frame, static_cast<int>(sizeof(frame)));
  TEST_ASSERT_TRUE(strstr(frame, "\"hosts\":2") != nullptr);
  TEST_ASSERT_TRUE(strstr(frame, "\"svc\":\"s/5120/5120/256/5120\"") != nullptr);
  TEST_ASSERT_EQUAL_INT(allFrame, n);
  TEST_ASSERT_TRUE(n < 512);
  TEST_ASSERT_TRUE(strstr(frame, "password") == nullptr);
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
  RUN_TEST(test_name_sanitize_and_precedence);
  RUN_TEST(test_inventory_name_preserves_host_and_allows_duplicates);
  RUN_TEST(test_ui_host_detail_with_and_without_name);
  RUN_TEST(test_host_card_lines_stay_in_one_region);
  RUN_TEST(test_host_cards_do_not_share_fields);
  RUN_TEST(test_ptr_question_and_reply);
  RUN_TEST(test_oui_parse_classify_and_lookup);
  RUN_TEST(test_inventory_oui_preserves_host_without_table);
  RUN_TEST(test_ui_manufacturer_states);
  RUN_TEST(test_action_parity_touch_and_direct);
  RUN_TEST(test_app_state_has_no_secret);
  RUN_TEST(test_inventory_csv_escape_and_publish);
  RUN_TEST(test_ui_scan_and_persist_copy);
  RUN_TEST(test_remote_session_and_state);
  RUN_TEST(test_remote_touch_parity);
  RUN_TEST(test_remote_result_rows);
  RUN_TEST(test_networks_back_returns_home);
  RUN_TEST(test_remote_visual_ack_matches_touch_face);
  RUN_TEST(test_dirty_regions_and_progress);
  RUN_TEST(test_settings_and_service_profile);
  RUN_TEST(test_control_affordance_and_address_range);
  RUN_TEST(test_remote_action_busy_result);
  RUN_TEST(test_resource_line_injected);
  RUN_TEST(test_service_profiles_and_scan);
  RUN_TEST(test_service_result_ux);
  gFailures = UNITY_END();
}

void loop() {}

int main() {
  setup();
  return gFailures;
}
