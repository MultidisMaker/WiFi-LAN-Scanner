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
  const bool enrichIdle = now == ScanState::Complete && nameEnrichmentIdle(scanner) && dnsEnrichmentIdle(scanner) &&
                          ouiEnrichmentIdle(scanner);
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
  noteResourceMilestones(gScanner);
  gUi.loop();
#if WLS_TEST_MODE
  hilPoll();
#else
  usbRemotePoll();
#endif
}
