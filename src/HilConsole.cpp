#include "HilConsole.h"

#if WLS_TEST_MODE

#include <Arduino.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include <esp_wifi.h>

#include "BoardConfig.h"
#include "DnsPtrEnricher.h"
#include "CandidatePlan.h"
#include "DeviceContext.h"
#include "FakeDiscovery.h"
#include "HostInventory.h"
#include "MdnsEnricher.h"
#include "NameRecord.h"
#include "Oui.h"
#include "OuiData.h"
#include "AppActions.h"
#include "InventoryExport.h"
#include "InventoryStore.h"
#include "OuiEnricher.h"
#include "ResourceFormat.h"
#include "ResourceMeter.h"
#include "NetMath.h"
#include "PasswordBuffer.h"
#include "ScannerController.h"
#include "UiModel.h"
#include "UiPress.h"
#include "UsbRemote.h"

namespace {
UiSnapshot gSnapshot;
ScannerController gHilScanner;
FakeDiscoveryBackend gHilBackend;
HostInventory gHilDuplicate;
CandidatePlan gLivePlan;
char gLine[416];
size_t gUsed = 0;
bool gOverflow = false;
char gExportCsv[2048];
char gExportCsvAgain[2048];
InventoryRow gExportRows[8];

struct HilRamFile {
  char path[80];
  char body[2048];
  bool used;
};

HilRamFile gHilRam[4];

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

struct LiveHold {
  bool armed = false;
  bool pauseOk = false;
  bool resumeOk = false;
  bool completeOk = false;
  bool dupOk = false;
  bool gatewaySeen = false;
  uint16_t seen = 0;
  NetFacts facts;
};

LiveHold gLiveHold;

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
  scanner.setAutomatic();
  scanner.setLimit(256);
  reportResource("before-scan");
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
    serviceOuiEnrichment(scanner);
    ++guard;
  }
  reportResource("after-scan");
  const bool completeOk = scanner.state() == ScanState::Complete && scanner.processedCount() == scanner.candidateCount() &&
                          scanner.candidateCount() == gLivePlan.count && scanner.candidateCount() <= kCandidateCap;
  const uint16_t seen = scanner.observedCount();
  const uint32_t enrichStart = millis();
  const uint32_t enrichBudget = static_cast<uint32_t>(seen) * 550u + 1500u;
  while (!dnsEnrichmentIdle(scanner) && static_cast<uint32_t>(millis() - enrichStart) < enrichBudget) {
    serviceNameEnrichment(scanner);
    serviceDnsEnrichment(scanner);
    serviceOuiEnrichment(scanner);
    delay(20);
    deviceUiLoop();
  }
  for (int step = 0; step < 16 && !ouiEnrichmentIdle(scanner); ++step) {
    serviceOuiEnrichment(scanner);
  }
  reportResource("after-enrich");
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
    const char* org = host->ouiState == OuiState::Known && host->manufacturer != nullptr && host->manufacturer[0] != '\0'
                          ? host->manufacturer
                          : "none";
    Serial.printf("WLS-HIL HOST ip=%s mac=%s name=%s source=%s oui=%s org=%s\n", hostIp,
                  host->hasMac ? hostMac : "none", host->name[0] != '\0' ? host->name : "none",
                  nameSourceLabel(host->nameSource), ouiStateLabel(host->ouiState), org);
  }
  const uint16_t queried = dnsEnrichmentAttemptCount();
  const uint16_t skipped = seen > queried ? static_cast<uint16_t>(seen - queried) : 0;
  Serial.printf("WLS-HIL ENRICH queried=%u named=%u skipped=%u dnsAttempts=%u\n", nameEnrichmentQueryCount(), named, skipped,
                queried);

  reportResource("before-persist");
  const InventoryStoreResult stored = storeInventoryOnSd();
  reportResource("after-persist");
  const char* liveLabel = "fail";
  if (stored.status == InventoryStoreStatus::Stored) {
    liveLabel = "stored";
  } else if (stored.status == InventoryStoreStatus::Absent) {
    liveLabel = "absent";
  } else if (stored.status == InventoryStoreStatus::Unavailable) {
    liveLabel = "unavailable";
  }
  Serial.printf("WLS-HIL PERSIST live=%s\n", liveLabel);

  gLiveHold = LiveHold();
  gLiveHold.armed = true;
  gLiveHold.pauseOk = pauseOk;
  gLiveHold.resumeOk = resumeOk;
  gLiveHold.completeOk = completeOk;
  gLiveHold.dupOk = dupOk;
  gLiveHold.gatewaySeen = gatewaySeen;
  gLiveHold.seen = seen;
  gLiveHold.facts = facts;
  Serial.printf("WLS-HIL LIVE hold=1 seen=%u\n", seen);
}

void hilLiveClose() {
  if (!gLiveHold.armed) {
    Serial.println("WLS-HIL LIVE pass=0 pause=0 resume=0 stop=0 complete=0 reset=0 seen=0 dup=0 gatewaySeen=0");
    return;
  }
  ScannerController& scanner = deviceScanner();
  scanner.reset();
  scanner.armConnectedFacts(gLiveHold.facts);
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
  reportResource("after-reset");
  const bool resetOk = scanner.state() == ScanState::Idle && scanner.observedCount() == 0 && !scanner.hasCurrent();
  forgetVolatileSta();
  const bool livePass = gLiveHold.pauseOk && gLiveHold.resumeOk && stopOk && gLiveHold.completeOk && resetOk &&
                        gLiveHold.dupOk && gLiveHold.seen > 0;
  Serial.printf(
      "WLS-HIL LIVE pass=%d pause=%d resume=%d stop=%d complete=%d reset=%d seen=%u dup=%d gatewaySeen=%d\n",
      livePass ? 1 : 0, gLiveHold.pauseOk ? 1 : 0, gLiveHold.resumeOk ? 1 : 0, stopOk ? 1 : 0,
      gLiveHold.completeOk ? 1 : 0, resetOk ? 1 : 0, gLiveHold.seen, gLiveHold.dupOk ? 1 : 0,
      gLiveHold.gatewaySeen ? 1 : 0);
  gLiveHold.armed = false;
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
  int spin = 0;
  while (scanner.state() == ScanState::Starting && spin < 6) {
    delay(50);
    scanner.loop();
    ++spin;
  }
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
                      gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "aaa-lower", NameSource::ReverseDns) == NameApply::Applied &&
                      strcmp(gHilDuplicate.at(0)->name, "aaa-lower") == 0 &&
                      gHilDuplicate.at(0)->nameSource == NameSource::ReverseDns && gHilDuplicate.at(0)->mac[5] == 0x11;

  char longName[48];
  for (int i = 0; i < 47; ++i) {
    longName[i] = 'c';
  }
  longName[47] = '\0';
  const bool clippedOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 2), longName, NameSource::Mdns) == NameApply::Applied &&
                         strlen(gHilDuplicate.at(1)->name) == 31;
  const bool precedenceOk =
      gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "alpha", NameSource::Mdns) == NameApply::Kept &&
      strcmp(gHilDuplicate.at(0)->name, "aaa-lower") == 0 && gHilDuplicate.at(0)->nameSource == NameSource::ReverseDns;
  const bool sameOk = gHilDuplicate.rememberName(ipv4(10, 0, 0, 2), "alpha", NameSource::Mdns) == NameApply::Applied &&
                      gHilDuplicate.count() == 2 && strcmp(gHilDuplicate.at(0)->name, "aaa-lower") == 0 &&
                      gHilDuplicate.at(0)->nameSource == NameSource::ReverseDns &&
                      strcmp(gHilDuplicate.at(1)->name, "alpha") == 0 &&
                      gHilDuplicate.at(1)->nameSource == NameSource::Mdns;

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
                    strstr(detail0, "Name: aaa-lower ") == detail0 && strstr(detail0, "02:00:00:00:00:11") != nullptr &&
                    strstr(detail1, "Name: alpha ") == detail1 && strstr(detail1, "02:00:00:00:00:12") != nullptr;

  Serial.printf("WLS-HIL NAME ip=10.0.0.1 name=%s source=%s\n", gHilDuplicate.at(0)->name,
                nameSourceLabel(gHilDuplicate.at(0)->nameSource));
  Serial.printf("WLS-HIL NAMES named=%d blank=%d kept=%d clipped=%d precedence=%d same=%d ui=%d\n", namedOk ? 1 : 0,
                blankOk ? 1 : 0, keptOk ? 1 : 0, clippedOk ? 1 : 0, precedenceOk ? 1 : 0, sameOk ? 1 : 0, uiOk ? 1 : 0);
}

void hilOui() {
  static const char kNames[] = "Acme Widgets\0Hidden Vendor\0Group Vendor";
  static const OuiEntry kEntries[] = {
      {0x001122u, 0u},
      {0x011122u, 27u},
      {0x021122u, 13u},
  };
  const OuiTable table = {kEntries, 3u, kNames};
  const uint8_t knownMac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t unknownMac[6] = {0x00, 0x44, 0x55, 0x66, 0x77, 0x88};
  const uint8_t localMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  const uint8_t groupMac[6] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
  gHilDuplicate.clear();
  gHilDuplicate.observe(ipv4(10, 0, 0, 1), EvidenceRank::Neighbor, true, knownMac, true, 4, 10, "fake");
  gHilDuplicate.observe(ipv4(10, 0, 0, 2), EvidenceRank::Neighbor, true, unknownMac, true, 4, 11, "fake");
  gHilDuplicate.observe(ipv4(10, 0, 0, 3), EvidenceRank::Neighbor, true, localMac, true, 4, 12, "fake");
  gHilDuplicate.observe(ipv4(10, 0, 0, 4), EvidenceRank::Neighbor, true, groupMac, true, 4, 13, "fake");
  const bool named = gHilDuplicate.rememberName(ipv4(10, 0, 0, 1), "alpha", NameSource::Mdns) == NameApply::Applied;
  for (uint16_t i = 0; i < gHilDuplicate.count(); ++i) {
    gHilDuplicate.enrichManufacturer(i, table);
  }
  const ObservedHost* known = gHilDuplicate.at(0);
  const ObservedHost* unknown = gHilDuplicate.at(1);
  const ObservedHost* local = gHilDuplicate.at(2);
  const ObservedHost* group = gHilDuplicate.at(3);
  const bool knownOk = known != nullptr && known->ouiState == OuiState::Known && known->manufacturer != nullptr &&
                       strcmp(known->manufacturer, "Acme Widgets") == 0 && known->macClass == MacClass::Global;
  const bool unknownOk = unknown != nullptr && unknown->ouiState == OuiState::Unknown && unknown->manufacturer == nullptr &&
                         unknown->macClass == MacClass::Global;
  const bool localOk = local != nullptr && local->ouiState == OuiState::Local && local->manufacturer == nullptr &&
                       local->macClass == MacClass::Local;
  const bool groupOk = group != nullptr && group->ouiState == OuiState::Group && group->manufacturer == nullptr &&
                       group->macClass == MacClass::Group;
  const bool keptOk = named && knownOk && strcmp(known->name, "alpha") == 0 && known->nameSource == NameSource::Mdns &&
                      known->hasMac && known->mac[0] == 0x00 && ipv4Equal(known->ip, ipv4(10, 0, 0, 1));
  UiSnapshot snapshot;
  snapshot.phase = UiPhase::Hosts;
  for (int row = 0; row < 4; ++row) {
    const ObservedHost* host = gHilDuplicate.at(static_cast<uint16_t>(row));
    snapshot.rowPresent[row] = host != nullptr;
    if (host == nullptr) {
      continue;
    }
    formatIpv4(host->ip, snapshot.rowLabel[row], sizeof(snapshot.rowLabel[row]));
    formatHostDetail(snapshot.rowDetail[row], sizeof(snapshot.rowDetail[row]), host->nameSource, host->name, host->hasMac,
                     host->mac);
    formatOuiLine(snapshot.rowVendor[row], sizeof(snapshot.rowVendor[row]), host->ouiState, host->manufacturer);
  }
  UiControl controls[8];
  const int count = collectUiControls(controls, 8, snapshot);
  const char* vendor[4] = {};
  const char* detail0 = nullptr;
  for (int i = 0; i < count; ++i) {
    if (strcmp(controls[i].label, "10.0.0.1") == 0) {
      vendor[0] = controls[i].vendor;
      detail0 = controls[i].detail;
    } else if (strcmp(controls[i].label, "10.0.0.2") == 0) {
      vendor[1] = controls[i].vendor;
    } else if (strcmp(controls[i].label, "10.0.0.3") == 0) {
      vendor[2] = controls[i].vendor;
    } else if (strcmp(controls[i].label, "10.0.0.4") == 0) {
      vendor[3] = controls[i].vendor;
    }
  }
  const bool uiOk = vendor[0] != nullptr && vendor[1] != nullptr && vendor[2] != nullptr && vendor[3] != nullptr &&
                    strcmp(vendor[0], "Acme Widgets") == 0 && strcmp(vendor[1], "unknown") == 0 &&
                    strcmp(vendor[2], "local") == 0 && strcmp(vendor[3], "group") == 0 && detail0 != nullptr &&
                    strstr(detail0, "Name: alpha ") == detail0 && strstr(detail0, "00:11:22:33:44:55") != nullptr;
  const OuiTable embedded = embeddedOuiTable();
  const bool registryOk = embedded.entries != nullptr && embedded.names != nullptr && embedded.count >= 30000u;
  Serial.printf("WLS-HIL OUI ip=10.0.0.1 class=global state=known org=%s\n",
                known != nullptr && known->manufacturer != nullptr ? known->manufacturer : "none");
  Serial.printf("WLS-HIL OUIREG count=%u\n", embedded.count);
  Serial.printf("WLS-HIL OUIS known=%d unknown=%d local=%d group=%d kept=%d ui=%d registry=%d\n", knownOk ? 1 : 0,
                unknownOk ? 1 : 0, localOk ? 1 : 0, groupOk ? 1 : 0, keptOk ? 1 : 0, uiOk ? 1 : 0, registryOk ? 1 : 0);
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

HilRamFile* hilRamFind(const char* path) {
  if (path == nullptr) {
    return nullptr;
  }
  for (int i = 0; i < 4; ++i) {
    if (gHilRam[i].used && strcmp(gHilRam[i].path, path) == 0) {
      return &gHilRam[i];
    }
  }
  return nullptr;
}

bool hilRamWrite(void* context, const char* path, const char* text) {
  (void)context;
  if (path == nullptr || text == nullptr || strlen(path) >= sizeof(gHilRam[0].path) ||
      strlen(text) >= sizeof(gHilRam[0].body)) {
    return false;
  }
  HilRamFile* slot = hilRamFind(path);
  if (slot == nullptr) {
    for (int i = 0; i < 4; ++i) {
      if (!gHilRam[i].used) {
        slot = &gHilRam[i];
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

bool hilRamRename(void* context, const char* fromPath, const char* toPath) {
  (void)context;
  HilRamFile* src = hilRamFind(fromPath);
  if (src == nullptr || toPath == nullptr || strlen(toPath) >= sizeof(src->path)) {
    return false;
  }
  HilRamFile* dest = hilRamFind(toPath);
  if (dest != nullptr && dest != src) {
    dest->used = false;
  }
  memset(src->path, 0, sizeof(src->path));
  memcpy(src->path, toPath, strlen(toPath));
  return true;
}

void copyExport(char* dest, size_t cap, const char* text) {
  size_t n = 0;
  if (dest == nullptr || cap == 0) {
    return;
  }
  if (text != nullptr) {
    for (; text[n] != '\0' && n + 1 < cap; ++n) {
      dest[n] = text[n];
    }
  }
  dest[n] = '\0';
}

void hilResources() {
  ResourceSample sample;
  sample.heap = 1000;
  sample.minHeap = 800;
  sample.maxBlock = 700;
  sample.psram = 8388608;
  sample.freePsram = 7000000;
  sample.minPsram = 6900000;
  char line[180];
  const int written = formatResourceLine(line, static_cast<int>(sizeof(line)), "hil-injected", sample);
  const bool formatted =
      written > 0 &&
      strcmp(line, "WLS resource phase=hil-injected heap=1000 min=800 block=700 psram=8388608 freePsram=7000000 minPsram=6900000") ==
          0;
  if (formatted) {
    Serial.println(line);
  }
  reportResource("hil-device");
  Serial.printf("WLS-HIL RESOURCES pass=%d\n", formatted ? 1 : 0);
}

void hilPersist() {
  memset(gHilRam, 0, sizeof(gHilRam));
  memset(gExportRows, 0, sizeof(gExportRows));
  InventoryMeta meta;
  meta.sequence = 1;
  copyExport(meta.station, sizeof(meta.station), "10.0.0.5");
  meta.prefix = 28;
  copyExport(meta.gateway, sizeof(meta.gateway), "10.0.0.1");
  meta.candidates = 14;
  meta.cap = 256;

  ObservedHost missing;
  missing.ip = ipv4(10, 0, 0, 2);
  missing.method = "arp";
  missing.macClass = MacClass::Absent;
  missing.ouiState = OuiState::DataMissing;
  missing.manufacturer = "Hidden";
  inventoryRowFromHost(gExportRows[0], missing);

  copyExport(gExportRows[1].ip, sizeof(gExportRows[1].ip), "10.0.0.1");
  copyExport(gExportRows[1].mac, sizeof(gExportRows[1].mac), "00:11:22:33:44:55");
  copyExport(gExportRows[1].method, sizeof(gExportRows[1].method), "arp");
  copyExport(gExportRows[1].name, sizeof(gExportRows[1].name), "alpha");
  copyExport(gExportRows[1].nameSource, sizeof(gExportRows[1].nameSource), "mdns");
  copyExport(gExportRows[1].macClass, sizeof(gExportRows[1].macClass), "global");
  copyExport(gExportRows[1].ouiState, sizeof(gExportRows[1].ouiState), "known");
  copyExport(gExportRows[1].manufacturer, sizeof(gExportRows[1].manufacturer), "Acme, Widgets");

  gExportRows[2] = gExportRows[1];
  copyExport(gExportRows[2].ip, sizeof(gExportRows[2].ip), "10.0.0.3");
  copyExport(gExportRows[2].manufacturer, sizeof(gExportRows[2].manufacturer), "Say \"hi\"");

  gExportRows[3] = gExportRows[1];
  copyExport(gExportRows[3].ip, sizeof(gExportRows[3].ip), "10.0.0.4");
  copyExport(gExportRows[3].name, sizeof(gExportRows[3].name), "Line1\nLine2");
  copyExport(gExportRows[3].manufacturer, sizeof(gExportRows[3].manufacturer), "Tail Vendor");

  ObservedHost grouped;
  grouped.ip = ipv4(10, 0, 0, 6);
  grouped.hasMac = true;
  const uint8_t groupMac[6] = {0x01, 0x11, 0x22, 0x33, 0x44, 0x55};
  memcpy(grouped.mac, groupMac, sizeof(groupMac));
  grouped.method = "arp";
  grouped.macClass = MacClass::Group;
  grouped.ouiState = OuiState::Group;
  grouped.manufacturer = "Group Vendor";
  inventoryRowFromHost(gExportRows[4], grouped);

  ObservedHost local;
  local.ip = ipv4(10, 0, 0, 7);
  local.hasMac = true;
  const uint8_t localMac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
  memcpy(local.mac, localMac, sizeof(localMac));
  local.method = "arp";
  local.macClass = MacClass::Local;
  local.ouiState = OuiState::Local;
  local.manufacturer = "Local Vendor";
  inventoryRowFromHost(gExportRows[5], local);

  const bool formatted = formatInventoryCsv(gExportCsv, static_cast<int>(sizeof(gExportCsv)), meta, gExportRows, 6);
  const bool repeat = formatted && formatInventoryCsv(gExportCsvAgain, static_cast<int>(sizeof(gExportCsvAgain)), meta, gExportRows, 6) &&
                      strcmp(gExportCsv, gExportCsvAgain) == 0;
  const bool missingOk = gExportRows[0].mac[0] == '\0' && gExportRows[0].manufacturer[0] == '\0' &&
                         strcmp(gExportRows[0].ouiState, "unavailable") == 0 && strstr(gExportCsv, "Hidden") == nullptr;
  const bool commaOk = strstr(gExportCsv, "\"Acme, Widgets\"") != nullptr;
  const bool quoteOk = strstr(gExportCsv, "\"Say \"\"hi\"\"\"") != nullptr;
  const bool newlineOk = strstr(gExportCsv, "\"Line1\nLine2\"") != nullptr;
  const bool keptStates = strstr(gExportCsv, "Group Vendor") == nullptr && strstr(gExportCsv, "Local Vendor") == nullptr &&
                          strstr(gExportCsv, ",group,") != nullptr && strstr(gExportCsv, ",local,") != nullptr;
  const bool secret = strstr(gExportCsv, "psk") != nullptr || strstr(gExportCsv, "password") != nullptr ||
                      strstr(gExportCsv, "passphrase") != nullptr || strstr(gExportCsv, "Offline") != nullptr;
  char path[80];
  const bool pathOk = inventoryScanPath(path, sizeof(path), 1);
  PublishSink sink;
  sink.write = hilRamWrite;
  sink.rename = hilRamRename;
  const bool published = formatted && pathOk && publishText(sink, path, gExportCsv);
  char temporary[96];
  snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  HilRamFile* finalFile = hilRamFind(path);
  const bool roundtrip = published && repeat && missingOk && keptStates && finalFile != nullptr &&
                         strcmp(finalFile->body, gExportCsv) == 0 && hilRamFind(temporary) == nullptr &&
                         strstr(gExportCsv, inventoryCsvHeader()) != nullptr;
  Serial.printf("WLS-HIL PERSIST path=%s\n", path);
  Serial.printf("WLS-HIL PERSISTS roundtrip=%d comma=%d quote=%d newline=%d secret=%d sd=skipped\n", roundtrip ? 1 : 0,
                commaOk ? 1 : 0, quoteOk ? 1 : 0, newlineOk ? 1 : 0, secret ? 1 : 0);
}

struct HilTrace {
  int start;
  int scanning;
  int paused;
  int resumed;
  int stopped;
  int reset;
  int hosts;
  int nextPage;
  int prevPage;
  int closed;
};

void hilAct(AppView& view, bool viaControl, int id, AppAction direct, uint16_t heldCount) {
  view.observedCount = heldCount;
  int row = -1;
  const AppAction action = viaControl ? actionFromControl(id, &row) : direct;
  if (action == AppAction::SelectRow || action == AppAction::SetProfile) {
    view.rowOffset = row;
  }
  applyAppAction(action, view, gHilScanner, nullptr);
}

void fillHilTrace(HilTrace& out, bool viaControl) {
  const NetFacts facts = deriveNetFacts(ipv4(10, 0, 0, 5), ipv4(255, 255, 255, 240), ipv4(10, 0, 0, 1),
                                        ipv4(10, 0, 0, 1), ipv4(0, 0, 0, 0));
  gHilBackend.clearScripts();
  gHilBackend.setWaitMs(60000);
  gHilScanner.reset();
  gHilScanner.setBackend(&gHilBackend);
  gHilScanner.armConnectedFacts(facts);
  AppView view;
  hilAct(view, viaControl, IdStart, AppAction::StartScan, 0);
  out.start = gHilScanner.state() == ScanState::Starting ? 1 : 0;
  delay(kScannerTransitionMs + 40);
  gHilScanner.loop();
  out.scanning = gHilScanner.state() == ScanState::Scanning ? 1 : 0;
  hilAct(view, viaControl, IdPause, AppAction::PauseScan, 0);
  out.paused = gHilScanner.state() == ScanState::Paused ? 1 : 0;
  hilAct(view, viaControl, IdResume, AppAction::ResumeScan, 0);
  out.resumed = gHilScanner.state() == ScanState::Scanning ? 1 : 0;
  hilAct(view, viaControl, IdStop, AppAction::StopScan, 0);
  delay(kScannerTransitionMs + 40);
  gHilScanner.loop();
  out.stopped = gHilScanner.state() == ScanState::Complete ? 1 : 0;
  hilAct(view, viaControl, IdReset, AppAction::ResetScan, 0);
  out.reset = gHilScanner.state() == ScanState::Idle && gHilScanner.observedCount() == 0 ? 1 : 0;
  view = AppView();
  hilAct(view, viaControl, IdHosts, AppAction::OpenHosts, 13);
  out.hosts = view.showingHosts && view.page == 0 ? 1 : 0;
  hilAct(view, viaControl, IdNext, AppAction::NextPage, 13);
  out.nextPage = view.page;
  hilAct(view, viaControl, IdPrev, AppAction::PrevPage, 13);
  out.prevPage = view.page;
  hilAct(view, viaControl, IdBack, AppAction::Back, 13);
  out.closed = view.showingHosts ? 0 : 1;
}

bool hilTraceSame(const HilTrace& left, const HilTrace& right) {
  return left.start == right.start && left.scanning == right.scanning && left.paused == right.paused &&
         left.resumed == right.resumed && left.stopped == right.stopped && left.reset == right.reset &&
         left.hosts == right.hosts && left.nextPage == right.nextPage && left.prevPage == right.prevPage &&
         left.closed == right.closed;
}

void hilActions() {
  HilTrace touch;
  HilTrace direct;
  fillHilTrace(touch, true);
  fillHilTrace(direct, false);
  int row = -1;
  const bool prevOk = actionFromControl(IdPrev, &row) == AppAction::PrevPage && IdPrev != IdRow0 + 1;
  const bool rowOk = actionFromControl(IdRow0, &row) == AppAction::SelectRow && row == 0 && IdRow0 == 200;
  AppView view;
  view.showingHosts = true;
  AppWifiView wifi;
  wifi.phase = "idle";
  AppState state;
  fillAppState(state, view, gHilScanner, wifi);
  char line[240];
  const int written = formatAppStateLine(line, static_cast<int>(sizeof(line)), state);
  const bool stateOk = written > 0 && strstr(line, "password") == nullptr && strstr(line, "psk") == nullptr &&
                       strstr(line, "passphrase") == nullptr && strstr(line, "screen=hosts") != nullptr;
  if (stateOk) {
    Serial.println(line);
  }
  const bool hostsOk = touch.hosts == 1 && direct.hosts == 1 && touch.nextPage == 1 && direct.nextPage == 1 &&
                       touch.prevPage == 0 && direct.prevPage == 0 && touch.closed == 1 && direct.closed == 1 && prevOk &&
                       rowOk && stateOk;
  const bool same = hilTraceSame(touch, direct) && stateOk;
  Serial.printf("WLS-HIL ACTIONS start=%d pause=%d resume=%d stop=%d reset=%d hosts=%d same=%d\n",
                touch.start == 1 && touch.scanning == 1 && direct.start == 1 && direct.scanning == 1 ? 1 : 0,
                touch.paused == 1 && direct.paused == 1 ? 1 : 0, touch.resumed == 1 && direct.resumed == 1 ? 1 : 0,
                touch.stopped == 1 && direct.stopped == 1 ? 1 : 0, touch.reset == 1 && direct.reset == 1 ? 1 : 0,
                hostsOk ? 1 : 0, same ? 1 : 0);
}

void hilRange() {
  ScannerController& scanner = deviceScanner();
  scanner.reset();
  const NetFacts facts = deriveNetFacts(ipv4(10, 1, 0, 9), ipv4(255, 255, 0, 0), ipv4(10, 1, 5, 5), ipv4(10, 1, 0, 1),
                                        ipv4(0, 0, 0, 0));
  scanner.armConnectedFacts(facts);
  scanner.setAutomatic();
  scanner.setLimit(256);
  RangePreview preview = scanner.preview();
  bool pass = preview.valid && preview.count == 255 && !preview.gatewayForced && preview.gatewayIncluded &&
              preview.canNext && !preview.canPrev && ipv4Equal(preview.nextOrigin, ipv4(10, 1, 1, 0)) &&
              ipv4Equal(preview.start, ipv4(10, 1, 0, 1)) && ipv4Equal(preview.end, ipv4(10, 1, 0, 255));
  pass = pass && scanner.setLimit(64) && scanner.setCustomStart(ipv4(10, 1, 2, 10));
  preview = scanner.preview();
  pass = pass && preview.valid && preview.mode == RangeMode::Custom && preview.count == 64 && preview.limit == 64 &&
         !preview.clamped && ipv4Equal(preview.start, ipv4(10, 1, 2, 10)) && ipv4Equal(preview.end, ipv4(10, 1, 2, 73)) &&
         !preview.gatewayIncluded;
  pass = pass && !scanner.setCustomStart(ipv4(10, 2, 0, 1));
  pass = pass && !scanner.setCustomStart(ipv4(10, 1, 0, 0));
  pass = pass && !scanner.setCustomStart(ipv4(10, 1, 255, 255));
  preview = scanner.preview();
  pass = pass && ipv4Equal(preview.start, ipv4(10, 1, 2, 10));
  pass = pass && scanner.setCustomStart(ipv4(10, 1, 0, 9));
  preview = scanner.preview();
  pass = pass && preview.valid && ipv4Equal(preview.start, ipv4(10, 1, 0, 10));
  pass = pass && scanner.setLimit(256) && scanner.setCustomStart(ipv4(10, 1, 255, 200));
  preview = scanner.preview();
  pass = pass && preview.valid && preview.clamped && preview.count == 55 && preview.count <= kCandidateCap &&
         ipv4Equal(preview.end, ipv4(10, 1, 255, 254));
  pass = pass && !scanner.setLimit(512) && !scanner.setLimit(0);
  scanner.setAutomatic();
  scanner.setLimit(256);
  pass = pass && scanner.windowNext();
  preview = scanner.preview();
  pass = pass && preview.mode == RangeMode::Automatic && preview.count == 256 &&
         ipv4Equal(preview.start, ipv4(10, 1, 1, 0)) && ipv4Equal(preview.end, ipv4(10, 1, 1, 255)) &&
         !preview.gatewayIncluded;
  pass = pass && scanner.windowPrev();
  preview = scanner.preview();
  pass = pass && preview.mode == RangeMode::Automatic && !preview.gatewayForced && preview.gatewayIncluded &&
         preview.count == 255 && preview.limit == 256;
  scanner.setAutomatic();
  scanner.setLimit(256);
  scanner.reset();
  scanner.holdFacts(true);
  Serial.printf("WLS-HIL RANGE pass=%d\n", pass ? 1 : 0);
}

void hilDispatch(const char* line) {
  if (strncmp(line, "@R1 ", 4) == 0) {
    usbRemoteSubmitLine(line);
    return;
  }
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
  } else if (strcmp(line, "OUI") == 0) {
    hilOui();
  } else if (strcmp(line, "RESOURCES") == 0) {
    hilResources();
  } else if (strcmp(line, "ACTIONS") == 0) {
    hilActions();
  } else if (strcmp(line, "PERSIST") == 0) {
    hilPersist();
  } else if (strcmp(line, "LIVECLOSE") == 0) {
    hilLiveClose();
  } else if (strcmp(line, "RANGE") == 0) {
    hilRange();
  } else if (strcmp(line, "SDPROBE") == 0) {
    const SdProbeResult probe = probeSdMedia();
    if (strcmp(probe.result, "stored") == 0) {
      Serial.printf("WLS-HIL SDPROBE result=stored bytes=%lu match=%d removed=%d display=%s path=%s\n",
                    static_cast<unsigned long>(probe.bytes), probe.match ? 1 : 0, probe.removed ? 1 : 0,
                    probe.displayOk ? "ok" : "fail", probe.path != nullptr ? probe.path : "");
    } else if (strcmp(probe.result, "absent") == 0) {
      Serial.println("WLS-HIL SDPROBE result=absent display=ok");
    } else {
      Serial.printf("WLS-HIL SDPROBE result=fail stage=%s\n", probe.stage != nullptr ? probe.stage : "unknown");
    }
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
  if (usbRemoteStreaming()) {
    usbRemotePullOne();
    return;
  }
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
      const bool remote = gUsed >= 4 && memcmp(gLine, "@R1 ", 4) == 0;
      gUsed = 0;
      gOverflow = false;
      if (remote) {
        usbRemoteOversize();
      } else {
        Serial.println("WLS-HIL ERR line");
      }
      continue;
    }
    gLine[gUsed] = '\0';
    gUsed = 0;
    hilDispatch(gLine);
  }
}

#endif
