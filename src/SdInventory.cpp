#include "InventoryStore.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <stdio.h>
#include <string.h>

#include "BoardConfig.h"
#include "DisplayBoard.h"
#include "InventoryExport.h"
#include "NameRecord.h"
#include "NetMath.h"
#include "NetworkRange.h"
#include "Oui.h"
#include "ScannerController.h"
#include "ServiceScan.h"

namespace {

const ScannerController* gScanner = nullptr;
bool gSpiReady = false;
bool gMounted = false;
bool gAbsent = false;
bool gLegacyReported = false;
uint32_t gNextSequence = 1;
char gPreamble[512];
char gRowLine[512];

InventoryStoreResult makeResult(InventoryStoreStatus status, const char* detail, const char* path = nullptr) {
  InventoryStoreResult result;
  result.status = status;
  result.detail = detail;
  result.path[0] = '\0';
  if (path != nullptr && path[0] != '\0') {
    snprintf(result.path, sizeof(result.path), "%s", path);
  }
  rememberInventoryStore(result);
  return result;
}

void deselectBoth() {
  pinMode(kSdCs, OUTPUT);
  digitalWrite(kSdCs, HIGH);
  pinMode(kTftCs, OUTPUT);
  digitalWrite(kTftCs, HIGH);
}

bool ensureMounted() {
  if (gMounted) {
    deselectBoth();
    return true;
  }
  if (gAbsent) {
    deselectBoth();
    return false;
  }
  deselectBoth();
  if (!gSpiReady) {
    SPI.begin(kTftSck, kTftMiso, kTftMosi, kSdCs);
    gSpiReady = true;
  }
  const bool began = SD.begin(kSdCs, SPI, kSdSpiHz, "/sd", 5, false);
  if (!began || SD.cardType() == CARD_NONE) {
    gAbsent = true;
    if (began) {
      SD.end();
    }
    deselectBoth();
    return false;
  }
  gMounted = true;
  if (!gLegacyReported) {
    gLegacyReported = true;
    const bool legacy = SD.exists("/LANScanner/scans");
    Serial.printf("WLS sd legacy=%s\n", legacy ? "present" : "absent");
  }
  deselectBoth();
  return true;
}

bool ensureTree() {
  if (!SD.exists("/WiFi-LAN-Scanner") && !SD.mkdir("/WiFi-LAN-Scanner")) {
    return false;
  }
  if (!SD.exists("/WiFi-LAN-Scanner/scans") && !SD.mkdir("/WiFi-LAN-Scanner/scans")) {
    return false;
  }
  return true;
}

bool writeAll(File& file, const char* text) {
  if (text == nullptr) {
    return false;
  }
  const size_t length = strlen(text);
  return file.print(text) == length;
}

bool choosePath(char* path, size_t cap, uint32_t* sequence) {
  uint32_t candidate = gNextSequence == 0 ? 1 : gNextSequence;
  // The counter restarts at 1 on each boot. Earlier sessions keep
  // scan-########.csv, so this probe walks past those names.
  for (int attempt = 0; attempt < 1024; ++attempt) {
    if (!inventoryScanPath(path, cap, candidate)) {
      return false;
    }
    if (!SD.exists(path)) {
      *sequence = candidate;
      gNextSequence = candidate + 1;
      return true;
    }
    candidate += 1;
  }
  return false;
}

void fillMeta(InventoryMeta& meta, uint32_t sequence, const ScannerController& scanner) {
  meta = InventoryMeta();
  meta.sequence = sequence;
  const NetworkRange range = rangeFromStation();
  formatIp(range.address, meta.station, sizeof(meta.station));
  meta.prefix = range.prefix;
  formatIp(range.gateway, meta.gateway, sizeof(meta.gateway));
  meta.candidates = scanner.candidateCount();
  meta.cap = static_cast<uint16_t>(kFutureScanHostCap);
}

bool displayStillReady() {
  DisplayBoard& board = deviceDisplay();
  if (!board.ready() || board.width() != kPanelWidth || board.height() != kPanelHeight) {
    return false;
  }
  board.panel().fillScreen(BLACK);
  return board.ready() && board.width() == kPanelWidth && board.height() == kPanelHeight;
}

}  // namespace

void bindInventoryScanner(const ScannerController* scanner) { gScanner = scanner; }

InventoryStoreResult storeInventoryOnSd() {
  if (gScanner == nullptr) {
    return makeResult(InventoryStoreStatus::Failed, "unbound");
  }
  if (!ensureMounted()) {
    Serial.println("WLS sd status=absent detail=media-absent");
    return makeResult(InventoryStoreStatus::Absent, "media-absent");
  }
  if (!ensureTree()) {
    deselectBoth();
    Serial.println("WLS sd status=fail detail=mkdir");
    return makeResult(InventoryStoreStatus::Failed, "mkdir");
  }
  char path[80];
  uint32_t sequence = 0;
  if (!choosePath(path, sizeof(path), &sequence)) {
    deselectBoth();
    Serial.println("WLS sd status=fail detail=name-exhausted");
    return makeResult(InventoryStoreStatus::Failed, "name-exhausted");
  }
  InventoryMeta meta;
  fillMeta(meta, sequence, *gScanner);
  if (!formatInventoryPreamble(gPreamble, static_cast<int>(sizeof(gPreamble)), meta)) {
    deselectBoth();
    Serial.println("WLS sd status=fail detail=preamble");
    return makeResult(InventoryStoreStatus::Failed, "preamble");
  }
  char temporary[96];
  const int temporaryLength = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
  if (temporaryLength < 0 || static_cast<size_t>(temporaryLength) >= sizeof(temporary)) {
    deselectBoth();
    return makeResult(InventoryStoreStatus::Failed, "temp-path");
  }
  if (SD.exists(temporary)) {
    SD.remove(temporary);
  }
  deselectBoth();
  File file = SD.open(temporary, FILE_WRITE);
  if (!file) {
    deselectBoth();
    Serial.println("WLS sd status=fail detail=open");
    return makeResult(InventoryStoreStatus::Failed, "open");
  }
  bool wrote = writeAll(file, gPreamble);
  if (wrote) {
    const uint16_t count = gScanner->observedCount();
    for (uint16_t i = 0; wrote && i < count; ++i) {
      const ObservedHost* host = gScanner->hostAt(i);
      if (host == nullptr) {
        continue;
      }
      InventoryRow row;
      inventoryRowFromHost(row, *host);
      const ServiceScan* services = gScanner->serviceScan();
      if (services != nullptr) {
        formatServiceField(row.services, sizeof(row.services), services->profile(), services->resultAt(i));
      }
      if (!formatInventoryRowLine(gRowLine, static_cast<int>(sizeof(gRowLine)), row) || !writeAll(file, gRowLine)) {
        wrote = false;
      } else {
        // Same bytes just written. Bounded by the inventory cap so live CSV
        // identity can be compared with Remote without a second SD read.
        Serial.printf("WLS sd row=%s", gRowLine);
        char detail[48];
        char vendor[40];
        char ipText[16];
        formatHostDetail(detail, sizeof(detail), host->nameSource, host->name, host->hasMac, host->mac);
        formatOuiLine(vendor, sizeof(vendor), host->ouiState, host->manufacturer);
        formatIpv4(host->ip, ipText, sizeof(ipText));
        Serial.printf("WLS card ip=%s ~~ detail=%s ~~ vendor=%s\n", ipText, detail, vendor[0] != '\0' ? vendor : "none");
      }
    }
  }
  file.close();
  if (!wrote) {
    SD.remove(temporary);
    deselectBoth();
    Serial.println("WLS sd status=fail detail=write");
    return makeResult(InventoryStoreStatus::Failed, "write");
  }
  if (!SD.rename(temporary, path)) {
    SD.remove(temporary);
    deselectBoth();
    Serial.println("WLS sd status=fail detail=rename");
    return makeResult(InventoryStoreStatus::Failed, "rename");
  }
  File again = SD.open(path, FILE_READ);
  char signature[12] = {};
  const int readCount = again ? again.read(reinterpret_cast<uint8_t*>(signature), 11) : 0;
  if (again) {
    again.close();
  }
  deselectBoth();
  if (readCount != 11 || memcmp(signature, "# schema=2\n", 11) != 0) {
    Serial.println("WLS sd status=fail detail=readback");
    return makeResult(InventoryStoreStatus::Failed, "readback");
  }
  Serial.println("WLS sd status=stored detail=stored");
  Serial.printf("WLS sd path=%s\n", path);
  return makeResult(InventoryStoreStatus::Stored, "stored", path);
}

#if WLS_TEST_MODE
SdProbeResult probeSdMedia() {
  SdProbeResult probe;
  static const char kBody[] = "# schema=1\n# probe=A011-SD-HIL\nip,mac,method,name,nameSource,macClass,ouiState,manufacturer\n";
  static const char kPath[] = "/WiFi-LAN-Scanner/scans/a011-wls-sdhil.csv";
  if (!ensureMounted()) {
    probe.displayOk = displayStillReady();
    deselectBoth();
    if (!probe.displayOk) {
      probe.result = "fail";
      probe.stage = "display";
      return probe;
    }
    probe.result = "absent";
    probe.stage = "media";
    return probe;
  }
  if (!ensureTree()) {
    probe.result = "fail";
    probe.stage = "mkdir";
    deselectBoth();
    return probe;
  }
  char temporary[96];
  snprintf(temporary, sizeof(temporary), "%s.tmp", kPath);
  if (SD.exists(temporary)) {
    SD.remove(temporary);
  }
  File file = SD.open(temporary, FILE_WRITE);
  if (!file || !writeAll(file, kBody)) {
    if (file) {
      file.close();
    }
    SD.remove(temporary);
    probe.result = "fail";
    probe.stage = "write";
    deselectBoth();
    return probe;
  }
  file.close();
  File staged = SD.open(temporary, FILE_READ);
  static char gStaged[160];
  const int stagedCount = staged ? staged.read(reinterpret_cast<uint8_t*>(gStaged), sizeof(gStaged) - 1) : 0;
  if (staged) {
    staged.close();
  }
  if (stagedCount < 0) {
    SD.remove(temporary);
    probe.result = "fail";
    probe.stage = "write";
    deselectBoth();
    return probe;
  }
  gStaged[stagedCount] = '\0';
  if (strcmp(gStaged, kBody) != 0) {
    SD.remove(temporary);
    probe.result = "fail";
    probe.stage = "write";
    deselectBoth();
    return probe;
  }
  if (SD.exists(kPath)) {
    SD.remove(kPath);
  }
  if (!SD.rename(temporary, kPath)) {
    SD.remove(temporary);
    probe.result = "fail";
    probe.stage = "rename";
    deselectBoth();
    return probe;
  }
  File again = SD.open(kPath, FILE_READ);
  static char gReadback[160];
  const int readCount = again ? again.read(reinterpret_cast<uint8_t*>(gReadback), sizeof(gReadback) - 1) : 0;
  if (again) {
    again.close();
  }
  if (readCount < 0) {
    probe.result = "fail";
    probe.stage = "readback";
    deselectBoth();
    return probe;
  }
  gReadback[readCount] = '\0';
  probe.bytes = static_cast<uint32_t>(readCount);
  probe.match = strcmp(gReadback, kBody) == 0;
  if (!probe.match) {
    probe.result = "fail";
    probe.stage = "readback";
    deselectBoth();
    return probe;
  }
  probe.removed = SD.remove(kPath);
  if (SD.exists(temporary)) {
    SD.remove(temporary);
  }
  deselectBoth();
  probe.displayOk = displayStillReady();
  deselectBoth();
  if (!probe.removed) {
    probe.result = "fail";
    probe.stage = "remove";
    return probe;
  }
  if (!probe.displayOk) {
    probe.result = "fail";
    probe.stage = "display";
    return probe;
  }
  probe.path = kPath;
  probe.result = "stored";
  probe.stage = "stored";
  return probe;
}
#endif
