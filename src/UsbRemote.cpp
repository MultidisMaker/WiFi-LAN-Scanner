#include "UsbRemote.h"

#include <Arduino.h>
#include <string.h>

#include "InventoryExport.h"
#include "NetMath.h"
#include "RemoteProtocol.h"
#include "ScannerUi.h"
#include "ServiceScan.h"
#include "TouchBoard.h"

namespace {

ScannerUi* gUi = nullptr;
ScannerController* gScanner = nullptr;
RemoteSession gSession;
char gLine[416];
size_t gUsed = 0;
bool gDrop = false;

bool applyRemote(void* context, AppAction action, int rowOffset, const char* text) {
  (void)context;
  if (gUi == nullptr) {
    return false;
  }
  return gUi->applyRemote(action, rowOffset, text);
}

bool remoteRejectedBusy(void* context) {
  (void)context;
  return gUi != nullptr && gUi->remoteApplyWasBusy();
}

void loadRemoteState(void* context, AppState* out) {
  (void)context;
  if (out == nullptr) {
    return;
  }
  if (gUi == nullptr) {
    *out = AppState();
    return;
  }
  gUi->captureState(*out);
}

int remoteRows(void* context) {
  (void)context;
  if (gScanner == nullptr) {
    return 0;
  }
  return gScanner->observedCount();
}

bool remoteRow(void* context, int index, InventoryRow* out) {
  (void)context;
  if (out == nullptr || gScanner == nullptr || index < 0) {
    return false;
  }
  const ObservedHost* host = gScanner->hostAt(static_cast<uint16_t>(index));
  if (host == nullptr) {
    return false;
  }
  inventoryRowFromHost(*out, *host);
  return true;
}

bool remoteService(void* context, int index, ServiceWireRow* out) {
  (void)context;
  if (out == nullptr || gScanner == nullptr || index < 0) {
    return false;
  }
  const ObservedHost* host = gScanner->hostAt(static_cast<uint16_t>(index));
  if (host == nullptr) {
    return false;
  }
  *out = ServiceWireRow();
  formatIpv4(host->ip, out->ip, sizeof(out->ip));
  const ServiceScan* services = gScanner->serviceScan();
  if (services != nullptr) {
    const ServiceHostResult* result = services->resultAt(static_cast<uint16_t>(index));
    formatServiceWirePorts(out->ports, sizeof(out->ports), services->profile(), result);
    if (result != nullptr) {
      out->openCount = result->openCount;
    }
  }
  return true;
}

RemoteServices services() {
  RemoteServices value;
  value.apply = applyRemote;
  value.rejectedBusy = remoteRejectedBusy;
  value.loadState = loadRemoteState;
  value.rowCount = remoteRows;
  value.rowAt = remoteRow;
  value.serviceAt = remoteService;
  value.context = nullptr;
  return value;
}

void emit(int length, char* frame) {
  if (length > 0 && frame != nullptr && frame[0] != '\0') {
    Serial.print(frame);
  }
}

}  // namespace

void usbRemoteBind(ScannerUi* ui, ScannerController* scanner) {
  gUi = ui;
  gScanner = scanner;
}

bool usbRemoteStreaming() { return gSession.streaming; }

void usbRemoteSubmitLine(const char* line) {
  char frame[576];
  const RemoteServices bound = services();
  const int written = remoteSubmit(&gSession, line, frame, static_cast<int>(sizeof(frame)), &bound);
  emit(written, frame);
  if (written > 0 && strstr(frame, "\"op\":\"GOODBYE\"") != nullptr) {
    Serial.printf("WLS touch ready=%d\n", deviceTouch().ready() ? 1 : 0);
  }
}

void usbRemotePullOne() {
  char frame[576];
  const RemoteServices bound = services();
  emit(remotePull(&gSession, frame, static_cast<int>(sizeof(frame)), &bound), frame);
}

void usbRemoteOversize() {
  char frame[96];
  emit(remoteMarkOversize(&gSession, frame, static_cast<int>(sizeof(frame))), frame);
}

void usbRemotePoll() {
  if (!Serial && gSession.link == RemoteLink::ConnectedUsb) {
    gSession = RemoteSession();
    gUsed = 0;
    gDrop = false;
  }
  if (gSession.streaming) {
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
      if (!gDrop && gUsed + 1 < sizeof(gLine)) {
        gLine[gUsed++] = value;
      } else {
        gDrop = true;
      }
      continue;
    }
    if (gDrop) {
      const bool remote = gUsed >= 4 && memcmp(gLine, "@R1 ", 4) == 0;
      gUsed = 0;
      gDrop = false;
      if (remote) {
        usbRemoteOversize();
      }
      return;
    }
    gLine[gUsed] = '\0';
    gUsed = 0;
    if (strncmp(gLine, "@R1 ", 4) == 0) {
      usbRemoteSubmitLine(gLine);
    }
    return;
  }
}
