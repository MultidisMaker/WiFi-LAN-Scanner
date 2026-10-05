#include "HilConsole.h"

#if WLS_TEST_MODE

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "NetMath.h"
#include "PasswordBuffer.h"
#include "ScannerController.h"
#include "UiModel.h"
#include "UiPress.h"

namespace {
UiSnapshot gSnapshot;
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
  ScannerController scanner;
  const bool pass = pressTrackerSelfTest() && keyGlyphSelfTest() && maskPasswordSelfTest() &&
                    buffer.preservedAcrossShift() && nets && scanner.selfTest();
  Serial.printf("WLS-HIL SELF pass=%d\n", pass ? 1 : 0);
}

void hilKeys() {
  const bool upper = gSnapshot.shift;
  const bool letters = alphabetCaseIs(gSnapshot, upper);
  UiSnapshot page = gSnapshot;
  page.phase = UiPhase::Password;
  page.keyboardPage = 0;
  UiControl controls[40];
  const int count = collectUiControls(controls, 40, page);
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
  ScannerController scanner;
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
