#include <unity.h>

#include <string.h>

#include "BoardConfig.h"
#include "CandidatePlan.h"
#include "FakeDiscovery.h"
#include "HostInventory.h"
#include "NameRecord.h"
#include "NetMath.h"
#include "Oui.h"
#include "OuiData.h"
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
  TEST_ASSERT_FALSE(preferIncomingName(NameSource::Mdns, "printer", NameSource::ReverseDns, "aaa"));
  TEST_ASSERT_TRUE(preferIncomingName(NameSource::ReverseDns, "dns-name", NameSource::Mdns, "zzz"));
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
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "aaa-lower", NameSource::ReverseDns) == NameApply::Kept);
  TEST_ASSERT_EQUAL_STRING("printer", inventory.at(0)->name);

  char longName[48];
  for (int i = 0; i < 47; ++i) {
    longName[i] = 'c';
  }
  longName[47] = '\0';
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 2), "", NameSource::Mdns) == NameApply::Rejected);
  TEST_ASSERT_TRUE(inventory.at(1)->name[0] == '\0' && inventory.at(1)->hasMac && inventory.at(1)->mac[5] == 0x12);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 2), longName, NameSource::Mdns) == NameApply::Applied);
  TEST_ASSERT_EQUAL_UINT(31, strlen(inventory.at(1)->name));

  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 1), "alpha", NameSource::Mdns) == NameApply::Applied);
  TEST_ASSERT_TRUE(inventory.rememberName(ipv4(10, 0, 0, 2), "alpha", NameSource::Mdns) == NameApply::Applied);
  TEST_ASSERT_TRUE(inventory.count() == 2);
  TEST_ASSERT_EQUAL_STRING("alpha", inventory.at(0)->name);
  TEST_ASSERT_EQUAL_STRING("alpha", inventory.at(1)->name);
  TEST_ASSERT_TRUE(ipv4Equal(inventory.at(0)->ip, ipv4(10, 0, 0, 1)));
  TEST_ASSERT_TRUE(ipv4Equal(inventory.at(1)->ip, ipv4(10, 0, 0, 2)));

  inventory.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, macA, true, 9, 30, "fake");
  TEST_ASSERT_TRUE(inventory.count() == 2);
  TEST_ASSERT_EQUAL_STRING("alpha", inventory.at(0)->name);
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
  TEST_ASSERT_EQUAL_STRING("u:unknown MAC unknown", unnamed);
  formatHostDetail(named, sizeof(named), NameSource::Mdns, "alpha", true, mac);
  TEST_ASSERT_EQUAL_STRING("m:alpha 02:11:22:33:44:55", named);
  char dnsDetail[40];
  formatHostDetail(dnsDetail, sizeof(dnsDetail), NameSource::ReverseDns, "ns-host", true, mac);
  TEST_ASSERT_EQUAL_STRING("d:ns-host 02:11:22:33:44:55", dnsDetail);

  char stored[32];
  for (int i = 0; i < 31; ++i) {
    stored[i] = 'n';
  }
  stored[31] = '\0';
  char clipped[40];
  formatHostDetail(clipped, sizeof(clipped), NameSource::Mdns, stored, false, nullptr);
  TEST_ASSERT_EQUAL_STRING("m:nnnnnnnnnnnnn MAC unknown", clipped);

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
      TEST_ASSERT_EQUAL_STRING("m:alpha 02:11:22:33:44:55", controls[i].detail);
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
      TEST_ASSERT_EQUAL_STRING("u:unknown MAC unknown", controls[i].detail);
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
  RUN_TEST(test_oui_parse_classify_and_lookup);
  RUN_TEST(test_inventory_oui_preserves_host_without_table);
  RUN_TEST(test_ui_manufacturer_states);
  gFailures = UNITY_END();
}

void loop() {}

int main() {
  setup();
  return gFailures;
}
