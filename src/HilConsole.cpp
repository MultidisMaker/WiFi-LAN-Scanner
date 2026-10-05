#include "HilConsole.h"

#if WLS_TEST_MODE

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include <esp_wifi.h>

#include "BoardConfig.h"
#include "CandidatePlan.h"
#include "DeviceContext.h"
#include "FakeDiscovery.h"
#include "HostInventory.h"
#include "MdnsEnricher.h"
#include "NameRecord.h"
#include "NetMath.h"
#include "PasswordBuffer.h"
#include "ScannerController.h"
#include "UiModel.h"
#include "UiPress.h"

namespace {
UiSnapshot gSnapshot;
ScannerController gHilScanner;
FakeDiscoveryBackend gHilBackend;
HostInventory gHilDuplicate;
CandidatePlan gLivePlan;
char gLine[96];
size_t gUsed = 0;
bool gOverflow = false;

const char* faceName(ControlFace face) {
  switch (face) {
    case ControlFace::Normal:
      return "normal";
    case ControlFace::Pressed:
      return "pressed";
    case ControlFace::Latched:
      return "latched";
    case ControlFace::LatchedPressed:
      return "latchedpressed";
  }
  return "unknown";
}

void copyToken(char* dest, size_t destLen, const char* text) {
  size_t i = 0;
  if (text != nullptr) {
    for (; text[i] != '\0' && i + 1 < destLen; ++i) {
      dest[i] = text[i];
    }
  }
  dest[i] = '\0';
}

bool bounded(int value, int low, int high) { return value >= low && value <= high; }

bool readExact(uint8_t* dest, size_t len, uint32_t timeoutMs) {
  size_t got = 0;
  const uint32_t start = millis();
  while (got < len) {
    if (Serial.available() > 0) {
      const int raw = Serial.read();
      if (raw < 0) {
        continue;
      }
      dest[got++] = static_cast<uint8_t>(raw);
      continue;
    }
    if (millis() - start >= timeoutMs) {
      return false;
    }
    delay(1);
  }
  return true;
}

void discardSerial() {
  while (Serial.available() > 0) {
    (void)Serial.read();
  }
}

void forgetVolatileSta() {
  wifi_config_t blank = {};
  (void)esp_wifi_set_config(WIFI_IF_STA, &blank);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, false);
}

bool waitUntilProbe(ScannerController& scanner) {
  delay(kScannerTransitionMs);
  scanner.loop();
  if (scanner.state() == ScanState::Starting) {
    delay(30);
    scanner.loop();
  }
  return scanner.state() == ScanState::Scanning && scanner.hasCurrent();
}

bool onFactsSubnet(const Ipv4& ip, const NetFacts& facts) {
  for (int i = 0; i < 4; ++i) {
    if ((ip.octet[i] & facts.mask.octet[i]) != (facts.address.octet[i] & facts.mask.octet[i])) {
      return false;
    }
  }
  return true;
}

bool planStaysInside(const CandidatePlan& plan, const NetFacts& facts) {
  if (!plan.valid || plan.count == 0 || plan.count > kCandidateCap) {
    return false;
  }
  for (uint16_t i = 0; i < plan.count; ++i) {
    const Ipv4& ip = plan.address[i];
    if (!onFactsSubnet(ip, facts) || ipv4Equal(ip, facts.network) || ipv4Equal(ip, facts.broadcast) ||
        ipv4Equal(ip, facts.address)) {
      return false;
    }
  }
  return true;
}

bool inventoryUnique(const ScannerController& scanner) {
  const uint16_t count = scanner.observedCount();
  for (uint16_t i = 0; i < count; ++i) {
    const ObservedHost* left = scanner.hostAt(i);
    if (left == nullptr) {
      return false;
    }
    for (uint16_t j = static_cast<uint16_t>(i + 1); j < count; ++j) {
      const ObservedHost* right = scanner.hostAt(j);
      if (right != nullptr && ipv4Equal(left->ip, right->ip)) {
        return false;
      }
    }
  }
  return true;
}

void hilLive() {
  uint8_t ssidLen = 0;
  uint8_t passLen = 0;
  char ssid[33] = {};
  char passBuf[64] = {};
  bool frameOk = readExact(&ssidLen, 1, 2000) && ssidLen >= 1 && ssidLen <= 32 &&
                 readExact(reinterpret_cast<uint8_t*>(ssid), ssidLen, 2000);
  if (frameOk) {
    ssid[ssidLen] = '\0';
    frameOk = readExact(&passLen, 1, 2000) && passLen <= 63 &&
              readExact(reinterpret_cast<uint8_t*>(passBuf), passLen, 2000);
  }
  if (frameOk) {
    passBuf[passLen] = '\0';
  } else {
    memset(ssid, 0, sizeof(ssid));
    memset(passBuf, 0, sizeof(passBuf));
    discardSerial();
    Serial.println("WLS-HIL LIVE associated=0 status=frame");
    return;
  }

  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, passBuf);
  memset(passBuf, 0, sizeof(passBuf));
  WiFi.setAutoReconnect(false);

  const uint32_t connectStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectStart < 25000) {
    delay(100);
    deviceUiLoop();
  }
  if (WiFi.status() != WL_CONNECTED) {
    memset(ssid, 0, sizeof(ssid));
    Serial.printf("WLS-HIL LIVE associated=0 status=%d\n", static_cast<int>(WiFi.status()));
    forgetVolatileSta();
    return;
  }
  char joined[33] = {};
  {
    const String currentSsid = WiFi.SSID();
    const size_t joinedLen = currentSsid.length() < sizeof(joined) ? currentSsid.length() : sizeof(joined) - 1;
    memcpy(joined, currentSsid.c_str(), joinedLen);
    const bool ssidOk = joinedLen == ssidLen && memcmp(joined, ssid, ssidLen) == 0;
    memset(ssid, 0, sizeof(ssid));
    memset(joined, 0, sizeof(joined));
    if (!ssidOk) {
      Serial.println("WLS-HIL LIVE associated=0 status=ssid");
      forgetVolatileSta();
      return;
    }
  }

  const NetFacts facts = deriveNetFacts(ipv4(WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]),
                                        ipv4(WiFi.subnetMask()[0], WiFi.subnetMask()[1], WiFi.subnetMask()[2], WiFi.subnetMask()[3]),
                                        ipv4(WiFi.gatewayIP()[0], WiFi.gatewayIP()[1], WiFi.gatewayIP()[2], WiFi.gatewayIP()[3]),
                                        ipv4(WiFi.dnsIP(0)[0], WiFi.dnsIP(0)[1], WiFi.dnsIP(0)[2], WiFi.dnsIP(0)[3]),
                                        ipv4(WiFi.dnsIP(1)[0], WiFi.dnsIP(1)[1], WiFi.dnsIP(1)[2], WiFi.dnsIP(1)[3]));
  buildCandidatePlanInto(gLivePlan, facts);
  const bool inside = facts.valid && planStaysInside(gLivePlan, facts);
  char ip[16];
  char mask[16];
  char gateway[16];
  char dns[16];
  formatIpv4(facts.address, ip, sizeof(ip));
  formatIpv4(facts.mask, mask, sizeof(mask));
  formatIpv4(facts.gateway, gateway, sizeof(gateway));
  formatIpv4(facts.dnsPrimary, dns, sizeof(dns));
  Serial.printf("WLS-HIL NET ssid=%.32s ip=%s mask=%s prefix=%u gw=%s dns=%s candidates=%u cap=%u inside=%d gatewayIncluded=%d\n",
                WiFi.SSID().c_str(), ip, mask, facts.prefix, gateway, dns, gLivePlan.count, kCandidateCap, inside ? 1 : 0,
                gLivePlan.gatewayIncluded ? 1 : 0);
  if (!inside) {
    Serial.println("WLS-HIL LIVE pass=0 pause=0 resume=0 stop=0 complete=0 reset=0 seen=0 dup=0 gatewaySeen=0");
    forgetVolatileSta();
    return;
  }

  ScannerController& scanner = deviceScanner();
  scanner.reset();
  serviceNameEnrichment(scanner);
  scanner.armConnectedFacts(facts);
  scanner.start();
  const bool probeArmed = waitUntilProbe(scanner);
  const uint16_t held = scanner.processedCount();
  const uint16_t seenHeld = scanner.observedCount();
  if (probeArmed) {
    scanner.pause();
  }
  delay(250);
  scanner.loop();
  deviceUiLoop();
  const bool pauseOk = probeArmed && scanner.state() == ScanState::Paused && scanner.processedCount() == held &&
                       scanner.observedCount() == seenHeld;
  if (pauseOk) {
    scanner.resume();
  }
  const bool resumeOk = pauseOk && scanner.state() == ScanState::Scanning;
  int guard = 0;
  const int limit = static_cast<int>(scanner.candidateCount()) * 3 + 8;
  while (scanner.state() == ScanState::Scanning && guard < limit) {
    delay(kArpProbeWaitMs);
    scanner.loop();
    if (scanner.state() == ScanState::Scanning && !scanner.hasCurrent()) {
      scanner.loop();
    }
    deviceUiLoop();
    serviceNameEnrichment(scanner);
    ++guard;
  }
  const bool completeOk = scanner.state() == ScanState::Complete && scanner.processedCount() == scanner.candidateCount() &&
                          scanner.candidateCount() == gLivePlan.count && scanner.candidateCount() <= kCandidateCap;
  const uint16_t seen = scanner.observedCount();
  const uint32_t enrichStart = millis();
  const uint32_t enrichBudget = static_cast<uint32_t>(seen) * 400u + 500u;
  while (!nameEnrichmentIdle(scanner) && static_cast<uint32_t>(millis() - enrichStart) < enrichBudget) {
    serviceNameEnrichment(scanner);
    delay(20);
    deviceUiLoop();
  }
  const bool dupOk = inventoryUnique(scanner);
  bool gatewaySeen = false;
  uint16_t named = 0;
  for (uint16_t i = 0; i < scanner.observedCount(); ++i) {
    const ObservedHost* host = scanner.hostAt(i);
    if (host == nullptr) {
      continue;
    }
    if (ipv4Equal(host->ip, facts.gateway)) {
      gatewaySeen = true;
    }
    if (host->name[0] != '\0') {
      ++named;
    }
    char hostIp[16];
    char hostMac[18];
    formatIpv4(host->ip, hostIp, sizeof(hostIp));
    if (host->hasMac) {
      formatMac(host->mac, hostMac, sizeof(hostMac));
    } else {
      hostMac[0] = '\0';
    }
    Serial.printf("WLS-HIL HOST ip=%s mac=%s name=%s source=%s\n", hostIp, host->hasMac ? hostMac : "none",
                  host->name[0] != '\0' ? host->name : "none", nameSourceLabel(host->nameSource));
  }
  const uint16_t queried = nameEnrichmentQueryCount();
  const uint16_t skipped = seen > queried ? static_cast<uint16_t>(seen - queried) : 0;
  Serial.printf("WLS-HIL ENRICH queried=%u named=%u skipped=%u\n", queried, named, skipped);

  scanner.reset();
  scanner.armConnectedFacts(facts);
  scanner.start();
  const bool stopArmed = waitUntilProbe(scanner);
  const uint16_t partial = scanner.observedCount();
  if (stopArmed) {
    scanner.stop();
  }
  delay(kScannerTransitionMs);
  scanner.loop();
  if (scanner.state() == ScanState::Stopping) {
    delay(30);
    scanner.loop();
  }
  const bool stopOk = stopArmed && scanner.state() == ScanState::Complete && scanner.observedCount() == partial;
  scanner.reset();
  const bool resetOk = scanner.state() == ScanState::Idle && scanner.observedCount() == 0 && !scanner.hasCurrent();
  forgetVolatileSta();
  const bool livePass = pauseOk && resumeOk && stopOk && completeOk && resetOk && dupOk && seen > 0;
  Serial.printf(
      "WLS-HIL LIVE pass=%d pause=%d resume=%d stop=%d complete=%d reset=%d seen=%u dup=%d gatewaySeen=%d\n",
      livePass ? 1 : 0, pauseOk ? 1 : 0, resumeOk ? 1 : 0, stopOk ? 1 : 0, completeOk ? 1 : 0, resetOk ? 1 : 0, seen,
      dupOk ? 1 : 0, gatewaySeen ? 1 : 0);
}

void hilSelf() {
  const NetFacts slash24 = deriveNetFacts(ipv4(192, 168, 0, 20), ipv4(255, 255, 255, 0), ipv4(192, 168, 0, 1),
                                          ipv4(192, 168, 0, 1), ipv4(0, 0, 0, 0));
  const NetFacts slash28 = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                          ipv4(1, 1, 1, 1), ipv4(8, 8, 8, 8));
  const NetFacts slash16 = deriveNetFacts(ipv4(10, 1, 0, 9), ipv4(255, 255, 0, 0), ipv4(10, 1, 0, 1),
                                          ipv4(10, 1, 0, 1), ipv4(0, 0, 0, 0));
  const NetFacts broken = deriveNetFacts(ipv4(10, 0, 0, 1), ipv4(255, 0, 255, 0), ipv4(10, 0, 0, 1),
                                         ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  const bool nets = slash24.valid && slash24.prefix == 24 && slash24.usableHosts == 254 && slash24.futureScanCount == 254 &&
                    ipv4Equal(slash24.network, ipv4(192, 168, 0, 0)) && slash28.valid && slash28.prefix == 28 &&
                    slash28.usableHosts == 14 && slash28.futureScanCount == 14 && slash28.hasSecondaryDns && slash16.valid &&
                    slash16.prefix == 16 && slash16.usableHosts == 65534 && slash16.futureScanCount == kFutureScanHostCap &&
                    !broken.valid && !slash28.network.octet[3];
  PasswordBuffer buffer;
  const bool pass = pressTrackerSelfTest() && keyGlyphSelfTest() && maskPasswordSelfTest() &&
                    buffer.preservedAcrossShift() && nets && gHilScanner.selfTest();
  Serial.printf("WLS-HIL SELF pass=%d\n", pass ? 1 : 0);
}

void hilKeys() {
  const bool upper = gSnapshot.shift;
  const bool letters = alphabetCaseIs(gSnapshot, upper);
  UiSnapshot page = gSnapshot;
  page.phase = UiPhase::Password;
  page.keyboardPage = 0;
  int cap = 0;
  UiControl* controls = uiScratchControls(&cap);
  const int count = collectUiControls(controls, cap, page);
  ControlFace face = ControlFace::Normal;
  bool found = false;
  for (int i = 0; i < count; ++i) {
    if (controls[i].id == IdShift) {
      face = controlFace(controls[i].latched, false);
      found = true;
    }
  }
  const bool faceOk = found && face == (upper ? ControlFace::Latched : ControlFace::Normal);
  Serial.printf("WLS-HIL KEYS labels=%s face=%s pass=%d\n", upper ? "upper" : "lower", faceName(face),
                letters && faceOk ? 1 : 0);
}

void hilPreserve() {
  PasswordBuffer buffer;
  const bool preserved = buffer.preservedAcrossShift();
  char masked[8];
  maskPassword(masked, sizeof(masked), 4);
  const bool mask = masked[0] == '*' && masked[1] == '*' && masked[2] == '*' && masked[3] == '*' && masked[4] == '\0';
  Serial.printf("WLS-HIL PRESERVE preserved=%d mask=%d\n", preserved ? 1 : 0, mask ? 1 : 0);
}

void hilScan() {
  ScannerController& scanner = gHilScanner;
  FakeDiscoveryBackend& backend = gHilBackend;
  scanner.reset();
  backend.clearScripts();
  backend.setWaitMs(60000);
  scanner.setBackend(&backend);
  scanner.armConnectedFacts(deriveNetFacts(ipv4(192, 168, 0, 20), ipv4(255, 255, 255, 0), ipv4(192, 168, 0, 1),
                                           ipv4(192, 168, 0, 1), ipv4(0, 0, 0, 0)));
  bool ok = scanner.state() == ScanState::Idle;
  scanner.pause();
  scanner.resume();
  scanner.stop();
  scanner.acknowledge();
  ok = ok && scanner.state() == ScanState::Idle;
  scanner.start();
  ok = ok && scanner.state() == ScanState::Starting;
  scanner.pause();
  ok = ok && scanner.state() == ScanState::Starting;
  delay(50);
  scanner.loop();
  ok = ok && scanner.state() == ScanState::Starting;
  delay(200);
  scanner.loop();
  ok = ok && scanner.state() == ScanState::Scanning;
  scanner.loop();
  scanner.start();
  scanner.resume();
  scanner.acknowledge();
  ok = ok && scanner.state() == ScanState::Scanning;
  scanner.pause();
  ok = ok && scanner.state() == ScanState::Paused;
  scanner.start();
  scanner.pause();
  ok = ok && scanner.state() == ScanState::Paused;
  scanner.resume();
  ok = ok && scanner.state() == ScanState::Scanning;
  scanner.stop();
  ok = ok && scanner.state() == ScanState::Stopping;
  scanner.pause();
  ok = ok && scanner.state() == ScanState::Stopping;
  delay(50);
  scanner.loop();
  ok = ok && scanner.state() == ScanState::Stopping;
  delay(200);
  scanner.loop();
  ok = ok && scanner.state() == ScanState::Complete;
  scanner.stop();
  scanner.pause();
  ok = ok && scanner.state() == ScanState::Complete;
  scanner.acknowledge();
  ok = ok && scanner.state() == ScanState::Idle;
  Serial.printf("WLS-HIL SCAN pass=%d\n", ok ? 1 : 0);
}

void hilDiscover() {
  ScannerController& scanner = gHilScanner;
  FakeDiscoveryBackend& backend = gHilBackend;
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
  const NetFacts facts = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  scanner.reset();
  backend.clearScripts();
  backend.setWaitMs(0);
  backend.addObservation(ipv4(10, 0, 0, 1), true, mac, true, 5);
  backend.addObservation(ipv4(10, 0, 0, 2), false, nullptr, false, 0);
  scanner.setBackend(&backend);
  scanner.armConnectedFacts(facts);
  scanner.start();
  bool scanOk = scanner.state() == ScanState::Starting;
  delay(kScannerTransitionMs);
  scanner.loop();
  scanOk = scanOk && scanner.state() == ScanState::Scanning;

  int guard = 0;
  while (scanner.processedCount() < 1 && scanner.state() == ScanState::Scanning && guard < 8) {
    scanner.loop();
    ++guard;
  }
  const bool progress = scanner.processedCount() == 1 && scanner.observedCount() == 1 && scanner.hasLast() &&
                        ipv4Equal(scanner.lastAddress(), ipv4(10, 0, 0, 1));
  scanner.pause();
  const uint16_t seenAtPause = scanner.observedCount();
  const uint16_t processedAtPause = scanner.processedCount();
  scanner.loop();
  scanner.loop();
  const bool paused = scanner.state() == ScanState::Paused && scanner.observedCount() == seenAtPause &&
                      scanner.processedCount() == processedAtPause;
  scanner.resume();
  const bool resumed = scanner.state() == ScanState::Scanning;
  scanner.stop();
  delay(kScannerTransitionMs);
  scanner.loop();
  const bool stopKept = scanner.state() == ScanState::Complete && scanner.observedCount() == seenAtPause;

  scanner.reset();
  const bool resetIdle = scanner.state() == ScanState::Idle && scanner.observedCount() == 0 && !scanner.hasCurrent();
  scanner.setBackend(&backend);
  scanner.armConnectedFacts(facts);
  scanner.start();
  delay(kScannerTransitionMs);
  scanner.loop();
  guard = 0;
  while (scanner.state() == ScanState::Scanning && guard < 48) {
    scanner.loop();
    ++guard;
  }
  const ObservedHost* first = scanner.hostAt(0);
  const ObservedHost* second = scanner.hostAt(1);
  const bool complete = scanner.state() == ScanState::Complete && scanner.observedCount() == 2 &&
                        scanner.processedCount() == scanner.candidateCount();
  const bool macOk = first != nullptr && first->hasMac && ipv4Equal(first->ip, ipv4(10, 0, 0, 1)) && first->mac[0] == 0x02;
  const bool noMac = second != nullptr && !second->hasMac && ipv4Equal(second->ip, ipv4(10, 0, 0, 2));

  gHilDuplicate.clear();
  gHilDuplicate.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, mac, true, 5, 10, "fake");
  gHilDuplicate.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, mac, true, 5, 20, "fake");
  const ObservedHost* dupHost = gHilDuplicate.at(0);
  const bool dupOk = gHilDuplicate.count() == 1 && dupHost != nullptr && dupHost->lastSeenMs == 20 &&
                     dupHost->firstSeenMs == 10;

  Serial.printf(
      "WLS-HIL DISCOVER scan=%d progress=%d pause=%d resume=%d stopSeen=%d complete=%d hosts=%u mac=%d nomac=%d dup=%d "
      "reset=%d\n",
      scanOk ? 1 : 0, progress ? 1 : 0, paused ? 1 : 0, resumed ? 1 : 0, stopKept ? 1 : 0, complete ? 1 : 0,
      scanner.observedCount(), macOk ? 1 : 0, noMac ? 1 : 0, dupOk ? 1 : 0, resetIdle ? 1 : 0);
}

void hilNames() {
  const uint8_t macA[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x11};
  const uint8_t macB[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x12};
  gHilDuplicate.clear();
  gHilDuplicate.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, macA, true, 5, 10, "fake");
  gHilDuplicate.observe(ipv4(10, 0, 0, 2), EvidenceRank::Neighbor, true, macB, true, 6, 11, "fake");

  const bool namedOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "printer.local", NameSource::Mdns) == NameApply::Applied &&
                       gHilDuplicate.rememberName(ipv4(10, 0, 0, 9), "ghost", NameSource::Mdns) == NameApply::MissingHost &&
                       strcmp(gHilDuplicate.at(0)->name, "printer") == 0;
  const bool blankOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 2), "@@@", NameSource::Mdns) == NameApply::Rejected &&
                       gHilDuplicate.at(1)->name[0] == '\0' && gHilDuplicate.at(1)->hasMac &&
                       gHilDuplicate.at(1)->mac[5] == 0x12 && ipv4Equal(gHilDuplicate.at(1)->ip, ipv4(10, 0, 0, 2));
  const bool keptOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "\t\n", NameSource::Mdns) == NameApply::Rejected &&
                      gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "aaa-lower", NameSource::ReverseDns) == NameApply::Kept &&
                      strcmp(gHilDuplicate.at(0)->name, "printer") == 0 && gHilDuplicate.at(0)->mac[5] == 0x11;

  char longName[48];
  for (int i = 0; i < 47; ++i) {
    longName[i] = 'c';
  }
  longName[47] = '\0';
  const bool clippedOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 2), longName, NameSource::Mdns) == NameApply::Applied &&
                         strlen(gHilDuplicate.at(1)->name) == 31;
  const bool precedenceOk =
      gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "alpha", NameSource::Mdns) == NameApply::Applied &&
      strcmp(gHilDuplicate.at(0)->name, "alpha") == 0 && gHilDuplicate.at(0)->nameSource == NameSource::Mdns;
  const bool sameOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 2), "alpha", NameSource::Mdns) == NameApply::Applied &&
                      gHilDuplicate.count() == 2 && strcmp(gHilDuplicate.at(0)->name, gHilDuplicate.at(1)->name) == 0 &&
                      strcmp(gHilDuplicate.at(0)->name, "alpha") == 0;

  UiSnapshot snapshot;
  snapshot.phase = UiPhase::Hosts;
  snapshot.rowPresent[0] = true;
  snapshot.rowPresent[1] = true;
  formatIpv4(gHilDuplicate.at(0)->ip, snapshot.rowLabel[0], sizeof(snapshot.rowLabel[0]));
  formatIpv4(gHilDuplicate.at(1)->ip, snapshot.rowLabel[1], sizeof(snapshot.rowLabel[1]));
  formatHostDetail(snapshot.rowDetail[0], sizeof(snapshot.rowDetail[0]), gHilDuplicate.at(0)->nameSource,
                   gHilDuplicate.at(0)->name, gHilDuplicate.at(0)->hasMac, gHilDuplicate.at(0)->mac);
  formatHostDetail(snapshot.rowDetail[1], sizeof(snapshot.rowDetail[1]), gHilDuplicate.at(1)->nameSource,
                   gHilDuplicate.at(1)->name, gHilDuplicate.at(1)->hasMac, gHilDuplicate.at(1)->mac);
  UiControl controls[8];
  const int count = collectUiControls(controls, 8, snapshot);
  const char* detail0 = nullptr;
  const char* detail1 = nullptr;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.0.0.1") == 0) {
      detail0 = controls[i].detail;
    } else if (strcmp(controls[i].label, "10.0.0.2") == 0) {
      detail1 = controls[i].detail;
    }
  }
  const bool uiOk = detail0 != nullptr && detail1 != nullptr && strcmp(detail0, snapshot.rowDetail[0]) == 0 &&
                    strcmp(detail1, snapshot.rowDetail[1]) == 0 && strcmp(snapshot.rowLabel[0], "10.0.0.1") == 0 &&
                    strstr(detail0, "m:alpha ") == detail0 && strstr(detail1, "m:alpha ") == detail1;

  Serial.printf("WLS-HIL NAME ip=10.0.0.1 name=%s source=%s\n", gHilDuplicate.at(0)->name,
                nameSourceLabel(gHilDuplicate.at(0)->nameSource));
  Serial.printf("WLS-HIL NAMES named=%d blank=%d kept=%d clipped=%d precedence=%d same=%d ui=%d\n", namedOk ? 1 : 0,
                blankOk ? 1 : 0, keptOk ? 1 : 0, clippedOk ? 1 : 0, precedenceOk ? 1 : 0, sameOk ? 1 : 0, uiOk ? 1 : 0);
}

void hilUi(const char* line) {
  char mode[16] = {};
  int shift = 0;
  if (sscanf(line, "UI %15s %d", mode, &shift) < 1) {
    Serial.println("WLS-HIL ERR args");
    return;
  }
  gSnapshot = UiSnapshot();
  if (strcmp(mode, "home") == 0) {
    gSnapshot.phase = UiPhase::Home;
  } else if (strcmp(mode, "results") == 0) {
    gSnapshot.phase = UiPhase::Results;
    gSnapshot.rowPresent[0] = true;
    copyToken(gSnapshot.rowLabel[0], sizeof(gSnapshot.rowLabel[0]), "sample");
    copyToken(gSnapshot.rowDetail[0], sizeof(gSnapshot.rowDetail[0]), "open");
  } else if (strcmp(mode, "password") == 0) {
    gSnapshot.phase = UiPhase::Password;
    gSnapshot.shift = shift != 0;
  } else {
    Serial.println("WLS-HIL ERR args");
    return;
  }
  Serial.println("WLS-HIL UI ok");
}

void hilTap(const char* line) {
  int x = 0;
  int y = 0;
  int upMs = 0;
  int sampleMs = 0;
  if (sscanf(line, "TAP %d %d %d %d", &x, &y, &upMs, &sampleMs) != 4 || !bounded(x, -20, 600) ||
      !bounded(y, -20, 600) || !bounded(upMs, 0, 5000) || !bounded(sampleMs, 0, 5000)) {
    Serial.println("WLS-HIL ERR args");
    return;
  }
  const UiGesture gesture = playTap(gSnapshot, x, y, static_cast<uint32_t>(upMs), static_cast<uint32_t>(sampleMs));
  Serial.printf("WLS-HIL TAP hit=%s fire=%d cancel=%d shown=%d face=%s\n", uiControlName(gesture.hitId),
                gesture.fire ? 1 : 0, gesture.cancelled ? 1 : 0, gesture.shownAtSample, faceName(gesture.faceWhileDown));
}

void hilDrag(const char* line) {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  int moveMs = 0;
  int upMs = 0;
  if (sscanf(line, "DRAG %d %d %d %d %d %d", &x0, &y0, &x1, &y1, &moveMs, &upMs) != 6 || !bounded(x0, -20, 600) ||
      !bounded(y0, -20, 600) || !bounded(x1, -20, 600) || !bounded(y1, -20, 600) || !bounded(moveMs, 0, 5000) ||
      !bounded(upMs, 0, 5000)) {
    Serial.println("WLS-HIL ERR args");
    return;
  }
  const UiGesture gesture = playDrag(gSnapshot, x0, y0, x1, y1, static_cast<uint32_t>(moveMs), static_cast<uint32_t>(upMs));
  Serial.printf("WLS-HIL DRAG hit=%s fire=%d cancel=%d\n", uiControlName(gesture.hitId), gesture.fire ? 1 : 0,
                gesture.cancelled ? 1 : 0);
}

void hilDispatch(const char* line) {
  if (strcmp(line, "PING") == 0) {
    Serial.println("WLS-HIL PONG");
  } else if (strcmp(line, "SELF") == 0) {
    hilSelf();
  } else if (strcmp(line, "KEYS") == 0) {
    hilKeys();
  } else if (strcmp(line, "PRESERVE") == 0) {
    hilPreserve();
  } else if (strcmp(line, "SCAN") == 0) {
    hilScan();
  } else if (strcmp(line, "DISCOVER") == 0) {
    hilDiscover();
  } else if (strcmp(line, "NAMES") == 0) {
    hilNames();
  } else if (strcmp(line, "LIVE") == 0) {
    hilLive();
  } else if (strncmp(line, "UI ", 3) == 0) {
    hilUi(line);
  } else if (strncmp(line, "TAP ", 4) == 0) {
    hilTap(line);
  } else if (strncmp(line, "DRAG ", 5) == 0) {
    hilDrag(line);
  } else {
    Serial.println("WLS-HIL ERR unknown");
  }
}
}

void hilPoll() {
  while (Serial.available() > 0) {
    const int raw = Serial.read();
    if (raw < 0) {
      return;
    }
    const char value = static_cast<char>(raw);
    if (value == '\r') {
      continue;
    }
    if (value != '\n') {
      if (!gOverflow && gUsed + 1 < sizeof(gLine)) {
        gLine[gUsed++] = value;
      } else {
        gOverflow = true;
      }
      continue;
    }
    if (gOverflow) {
      gUsed = 0;
      gOverflow = false;
      Serial.println("WLS-HIL ERR line");
      continue;
    }
    gLine[gUsed] = '\0';
    gUsed = 0;
    hilDispatch(gLine);
  }
}

#endif
