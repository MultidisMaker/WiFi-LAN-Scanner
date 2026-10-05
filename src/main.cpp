#include <Arduino.h>

#include <WiFi.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "LwipArpBackend.h"
#include "NetMath.h"
#include "NetworkRange.h"
#include "ScannerController.h"
#include "ScannerUi.h"
#include "TouchBoard.h"
#include "UiPress.h"
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
  Serial.begin(115200);
  delay(200);
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
  gUi.begin(gWifi, gScanner);
  Serial.println("WLS ready discovery=local-arp");
#if WLS_TEST_MODE
  Serial.println("WLS-HIL ready");
#endif
}

void loop() {
  gWifi.loop();
  armScannerFromStation();
  gScanner.loop();
  gUi.loop();
#if WLS_TEST_MODE
  hilPoll();
#endif
}
