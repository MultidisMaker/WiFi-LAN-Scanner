#include <Arduino.h>

#include <WiFi.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "DnsPtrEnricher.h"
#include "LwipArpBackend.h"
#include "InventoryStore.h"
#include "MdnsEnricher.h"
#include "NetMath.h"
#include "OuiEnricher.h"
#include "NetworkRange.h"
#include "ResourceMeter.h"
#include "ScannerController.h"
#include "ScannerUi.h"
#include "ServiceConnect.h"
#include "ServiceScan.h"
#include "TouchBoard.h"
#include "UiPress.h"
#include "UsbRemote.h"
#include "WifiService.h"
#if WLS_TEST_MODE
#include "HilConsole.h"
#endif

namespace {
DisplayBoard gDisplay;
TouchBoard gTouch;
WifiService gWifi;
ScannerController gScanner;
LwipArpBackend gArp;
ScannerUi gUi;
ServiceScan gServices;
WifiTcpConnect gConnect;

struct ServiceClock {
  ScanState previous = ScanState::Idle;
  bool jobOpen = false;
  bool logged = false;
  uint16_t printedSerial = 0;
  uint32_t jobStartMs = 0;
  uint32_t discoveryEndMs = 0;
  uint32_t namingEndMs = 0;
  uint32_t serviceStartMs = 0;
  uint32_t serviceEndMs = 0;
};

ServiceClock gServiceClock;

void logServiceProbe() {
  const uint16_t serial = gServices.probeSerial();
  if (serial == 0 || serial == gServiceClock.printedSerial) {
    return;
  }
  Ipv4 ip;
  uint16_t port = 0;
  ServiceProbeClass outcome = ServiceProbeClass::None;
  if (gServices.lastProbe(ip, port, outcome)) {
    char text[16];
    formatIpv4(ip, text, sizeof(text));
    const char* name = "error";
    if (outcome == ServiceProbeClass::Open) {
      name = "open";
    } else if (outcome == ServiceProbeClass::Closed) {
      name = "closed";
    } else if (outcome == ServiceProbeClass::Timeout) {
      name = "timeout";
    }
    Serial.printf("WLS service result ip=%s port=%u state=%s\n", text, port, name);
  }
  gServiceClock.printedSerial = serial;
}

void tickServices() {
  const uint32_t now = millis();
  const ScanState state = gScanner.state();
  const ScanState previous = gServiceClock.previous;
  if (previous != ScanState::Starting && state == ScanState::Starting) {
    gServiceClock = ServiceClock();
    gServiceClock.jobOpen = true;
    gServiceClock.jobStartMs = now;
  }
  gServiceClock.previous = state;
  if (state == ScanState::Complete && gServiceClock.jobOpen && gServiceClock.discoveryEndMs == 0) {
    gServiceClock.discoveryEndMs = now;
  }
  const bool namingIdle = nameEnrichmentIdle(gScanner) && dnsEnrichmentIdle(gScanner) && ouiEnrichmentIdle(gScanner);
  if (namingIdle && gServiceClock.discoveryEndMs != 0 && gServiceClock.namingEndMs == 0) {
    gServiceClock.namingEndMs = now;
  }
  gServices.setProfile(gUi.profile());
  if (namingIdle && gScanner.armServiceScan(now)) {
    reportResource("before-services");
    gServiceClock.serviceStartMs = now;
    gServiceClock.logged = false;
    Serial.printf("WLS services arm profile=%s ports=%u targets=%u planned=%u timeoutMs=%lu\n",
                  serviceProfileToken(gServices.profile()), static_cast<unsigned>(serviceProfilePortCount(gServices.profile())),
                  gServices.targetCount(), gServices.planned(),
                  static_cast<unsigned long>(serviceProfileTimeoutMs(gServices.profile())));
  }
  gScanner.serviceLoop(now);
  logServiceProbe();
  if (!gServiceClock.logged && gServiceClock.serviceStartMs != 0 &&
      (gServices.run() == ServiceRun::Complete || gServices.run() == ServiceRun::Stopped)) {
    gServiceClock.serviceEndMs = now;
    reportResource("after-services");
    const uint32_t discoveryMs = gServiceClock.discoveryEndMs >= gServiceClock.jobStartMs
                                     ? gServiceClock.discoveryEndMs - gServiceClock.jobStartMs
                                     : 0;
    const uint32_t ptrMs = gServiceClock.namingEndMs >= gServiceClock.discoveryEndMs && gServiceClock.discoveryEndMs != 0
                               ? gServiceClock.namingEndMs - gServiceClock.discoveryEndMs
                               : 0;
    const uint32_t serviceMs = gServiceClock.serviceEndMs >= gServiceClock.serviceStartMs
                                   ? gServiceClock.serviceEndMs - gServiceClock.serviceStartMs
                                   : 0;
    const uint32_t totalMs =
        gServiceClock.jobStartMs != 0 && gServiceClock.serviceEndMs >= gServiceClock.jobStartMs
            ? gServiceClock.serviceEndMs - gServiceClock.jobStartMs
            : serviceMs;
    Serial.printf(
        "WLS services summary profile=%s ports=%u targets=%u planned=%u done=%u open=%u closed=%u timeout=%u error=%u "
        "openHosts=%u discoveryMs=%lu ptrMs=%lu serviceMs=%lu totalMs=%lu phase=%s\n",
        serviceProfileToken(gServices.profile()), static_cast<unsigned>(serviceProfilePortCount(gServices.profile())),
        gServices.targetCount(), gServices.planned(), gServices.completed(), gServices.openPorts(), gServices.closedCount(),
        gServices.timeoutCount(), gServices.errorCount(), gServices.openHosts(), static_cast<unsigned long>(discoveryMs),
        static_cast<unsigned long>(ptrMs), static_cast<unsigned long>(serviceMs), static_cast<unsigned long>(totalMs),
        gServices.run() == ServiceRun::Stopped ? "stop" : "done");
    gServiceClock.logged = true;
  }
}

struct ResourceGate {
  ScanState previous = ScanState::Idle;
  bool sawScan = false;
  bool sawEnrich = false;
};

ResourceGate gResourceGate;

void noteResourceMilestones(ScannerController& scanner) {
  const ScanState now = scanner.state();
  if (gResourceGate.previous != ScanState::Starting && now == ScanState::Starting) {
    reportResource("before-scan");
    gResourceGate.sawScan = false;
    gResourceGate.sawEnrich = false;
  }
  if (now == ScanState::Scanning && !gResourceGate.sawScan) {
    reportResource("scan");
    gResourceGate.sawScan = true;
  }
  if (gResourceGate.previous != ScanState::Complete && now == ScanState::Complete) {
    reportResource("after-scan");
  }
  const bool serviceIdle = scanner.serviceScan() == nullptr || scanner.serviceScan()->idle();
  const bool enrichIdle = now == ScanState::Complete && nameEnrichmentIdle(scanner) && dnsEnrichmentIdle(scanner) &&
                          ouiEnrichmentIdle(scanner) && serviceIdle;
  if (enrichIdle && !gResourceGate.sawEnrich) {
    reportResource("after-enrich");
    reportResource("before-persist");
    storeInventoryOnSd();
    reportResource("after-persist");
    gResourceGate.sawEnrich = true;
  }
  if (gResourceGate.previous != ScanState::Idle && now == ScanState::Idle) {
    reportResource("after-reset");
    gResourceGate.sawScan = false;
    gResourceGate.sawEnrich = false;
  }
  gResourceGate.previous = now;
}

void armScannerFromStation() {
  if (WiFi.status() != WL_CONNECTED) {
    gScanner.armDisconnected();
    return;
  }
  const NetworkRange range = rangeFromStation();
  gScanner.armConnectedFacts(deriveNetFacts(ipv4(range.address[0], range.address[1], range.address[2], range.address[3]),
                                             ipv4(range.mask[0], range.mask[1], range.mask[2], range.mask[3]),
                                             ipv4(range.gateway[0], range.gateway[1], range.gateway[2], range.gateway[3]),
                                             ipv4(range.dnsPrimary[0], range.dnsPrimary[1], range.dnsPrimary[2],
                                                  range.dnsPrimary[3]),
                                             ipv4(range.dnsSecondary[0], range.dnsSecondary[1], range.dnsSecondary[2],
                                                  range.dnsSecondary[3])));
}
}

DisplayBoard& deviceDisplay() { return gDisplay; }

TouchBoard& deviceTouch() { return gTouch; }

#if WLS_TEST_MODE
ScannerController& deviceScanner() { return gScanner; }

void deviceUiLoop() { gUi.loop(); }
#endif

void setup() {
  // The USB-CDC default queue is 256 bytes. A v1 frame may be 320 bytes
  // before its newline, so both queues have to hold one maximum frame.
  const size_t usbRx = Serial.setRxBufferSize(512);
  const size_t usbTx = Serial.setTxBufferSize(512);
  Serial.begin(115200);
  // A full USB-CDC transmit buffer otherwise waits forever and setup never
  // reaches the ready line on the HIL image.
  Serial.setTxTimeoutMs(1000);
  delay(200);
  Serial.printf("WLS usb-cdc rx=%u tx=%u\n", static_cast<unsigned>(usbRx), static_cast<unsigned>(usbTx));
  Serial.println("WLS boot WiFi-LAN-Scanner foundation");
  const bool displayOk = gDisplay.begin();
  Serial.printf("WLS display=%s geometry=%dx%d expected=%dx%d\n", displayOk ? "ok" : "fail", gDisplay.width(),
                gDisplay.height(), kPanelWidth, kPanelHeight);
  gTouch.begin();
  networkRangeSelfTest();
  gScanner.selfTest();
  const bool pressOk = pressTrackerSelfTest();
  const bool glyphOk = keyGlyphSelfTest();
  const bool maskOk = maskPasswordSelfTest();
  gWifi.begin();
  const bool preservedOk = gWifi.maskingSelfTest();
  Serial.printf("WLS ui-selftest=%s ackMs=%lu faces=4\n", pressOk && glyphOk && maskOk && preservedOk ? "ok" : "fail",
                static_cast<unsigned long>(PressTracker::kAckMs));
  Serial.printf("WLS mask-selftest=%s preserved=%s\n", maskOk && preservedOk ? "ok" : "fail", preservedOk ? "yes" : "no");
  gScanner.setBackend(&gArp);
  gServices.setBackend(&gConnect);
  gScanner.bindServiceScan(&gServices);
  bindInventoryScanner(&gScanner);
  gUi.begin(gWifi, gScanner);
  usbRemoteBind(&gUi, &gScanner);
  reportPsramProbe();
  reportResource("ready");
  Serial.println("WLS ready discovery=local-arp");
#if WLS_TEST_MODE
  Serial.println("WLS-HIL ready");
#endif
}

void loop() {
#if WLS_TEST_MODE
  static uint32_t hilBeatMs = 0;
  const uint32_t hilNow = millis();
  if (hilBeatMs == 0 || hilNow - hilBeatMs >= 1000) {
    hilBeatMs = hilNow;
    Serial.println("WLS-HIL beat");
    Serial.flush();
  }
#endif
  gWifi.loop();
  armScannerFromStation();
  gScanner.loop();
  serviceNameEnrichment(gScanner);
  serviceDnsEnrichment(gScanner);
  serviceOuiEnrichment(gScanner);
  tickServices();
  noteResourceMilestones(gScanner);
  gUi.loop();
#if WLS_TEST_MODE
  hilPoll();
#else
  usbRemotePoll();
#endif
}
