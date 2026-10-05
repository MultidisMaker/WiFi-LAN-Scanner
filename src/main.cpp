#include <Arduino.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "NetworkRange.h"
#include "ScannerController.h"
#include "ScannerUi.h"
#include "TouchBoard.h"
#include "WifiService.h"

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
  gWifi.begin();
  gUi.begin(gWifi, gScanner);
  Serial.println("WLS ready discovery=deferred");
}

void loop() {
  gWifi.loop();
  gScanner.loop();
  gUi.loop();
}
