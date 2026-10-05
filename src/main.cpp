#include <Arduino.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
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
ScannerUi gUi;
}

DisplayBoard& deviceDisplay() { return gDisplay; }

TouchBoard& deviceTouch() { return gTouch; }

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
  gUi.begin(gWifi, gScanner);
  Serial.println("WLS ready discovery=deferred");
#if WLS_TEST_MODE
  Serial.println("WLS-HIL ready");
#endif
}

void loop() {
  gWifi.loop();
  gScanner.loop();
  gUi.loop();
#if WLS_TEST_MODE
  hilPoll();
#endif
}
