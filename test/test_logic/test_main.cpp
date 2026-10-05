#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "ActionAck.h"
#include "AppActions.h"
#include "BoardConfig.h"
#include "CandidatePlan.h"
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

static int gPicked = -1;

static void rememberPick(void* context, int index) {
  (void)context;
  gPicked = index;
}

static void act(ScannerController& scanner, AppView& view, bool viaControl, int id, AppAction direct, const AppHooks* hooks) {
  int row = -1;
  const AppAction chosen = viaControl ? actionFromControl(id, &row) : direct;
  if (chosen == AppAction::SelectRow) {
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
  TEST_ASSERT_TRUE(strstr(csv, "# schema=1\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# sequence=1\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# station=10.0.0.5\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# prefix=28\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# gateway=10.0.0.1\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# candidates=14\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, "# cap=256\n") != nullptr);
  TEST_ASSERT_TRUE(strstr(csv, inventoryCsvHeader()) == csv + strlen("# schema=1\n# sequence=1\n# station=10.0.0.5\n# prefix=28\n# gateway=10.0.0.1\n# candidates=14\n# cap=256\n"));
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
  TEST_ASSERT_EQUAL_STRING("SD not written", full);
  TEST_ASSERT_TRUE(formatPersistPanel(panel, sizeof(panel), fresh));
  TEST_ASSERT_EQUAL_STRING("SD not written", panel);

  InventoryStoreResult unavailable;
  unavailable.detail = "contract-unproven";
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), unavailable));
  TEST_ASSERT_EQUAL_STRING("SD unavailable", full);

  InventoryStoreResult absent;
  absent.status = InventoryStoreStatus::Absent;
  absent.detail = "media-absent";
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), absent));
  TEST_ASSERT_EQUAL_STRING("SD absent", full);

  InventoryStoreResult failed;
  failed.status = InventoryStoreStatus::Failed;
  failed.detail = "write";
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), failed));
  TEST_ASSERT_EQUAL_STRING("SD error", full);

  InventoryStoreResult stored;
  stored.status = InventoryStoreStatus::Stored;
  stored.detail = "stored";
  snprintf(stored.path, sizeof(stored.path), "/WiFi-LAN-Scanner/scans/scan-00000001.csv");
  TEST_ASSERT_TRUE(formatPersistStatus(full, sizeof(full), stored));
  TEST_ASSERT_EQUAL_STRING("SD stored /WiFi-LAN-Scanner/scans/scan-00000001.csv", full);
  TEST_ASSERT_TRUE(formatPersistPanel(panel, sizeof(panel), stored));
  TEST_ASSERT_EQUAL_STRING("Stored /WiFi-LAN-Scanner/scans/", panel);
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

static bool worldApply(void* context, AppAction action, int rowOffset) {
  auto* world = static_cast<RemoteWorld*>(context);
  if (world == nullptr || world->scanner == nullptr || world->view == nullptr) {
    return false;
  }
  world->applies += 1;
  world->last = action;
  world->lastRow = rowOffset;
  if (action == AppAction::SelectRow) {
    world->view->rowOffset = rowOffset;
  }
  applyAppAction(action, *world->view, *world->scanner, nullptr);
  return true;
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
  fillAppState(*out, *world->view, *world->scanner, wifi);
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

void test_remote_visual_ack_matches_touch_face(void) {
  UiControl controls[16];
  UiSnapshot home;
  home.phase = UiPhase::Home;
  home.showDashboard = true;
  int count = collectUiControls(controls, 16, home);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::FindNetworks, -1, controls, count) == IdFind);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::Back, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::StartScan, -1, controls, count) == IdStart);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::PauseScan, -1, controls, count) == IdPause);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::ResumeScan, -1, controls, count) == IdResume);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::StopScan, -1, controls, count) == IdStop);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::ResetScan, -1, controls, count) == IdReset);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::OpenHosts, -1, controls, count) == IdHosts);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::NextPage, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::PrevPage, -1, controls, count) == -1);
  TEST_ASSERT_TRUE(visibleControlForAction(AppAction::SelectRow, 0, controls, count) == -1);

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

static bool busyApply(void* context, AppAction action, int rowOffset) {
  (void)context;
  (void)action;
  (void)rowOffset;
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
  RUN_TEST(test_action_parity_touch_and_direct);
  RUN_TEST(test_app_state_has_no_secret);
  RUN_TEST(test_inventory_csv_escape_and_publish);
  RUN_TEST(test_ui_scan_and_persist_copy);
  RUN_TEST(test_remote_session_and_state);
  RUN_TEST(test_remote_touch_parity);
  RUN_TEST(test_remote_result_rows);
  RUN_TEST(test_networks_back_returns_home);
  RUN_TEST(test_remote_visual_ack_matches_touch_face);
  RUN_TEST(test_remote_action_busy_result);
  RUN_TEST(test_resource_line_injected);
  gFailures = UNITY_END();
}

void loop() {}

int main() {
  setup();
  return gFailures;
}
