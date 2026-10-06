#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AppActions.h"
#include "InventoryExport.h"

// Remote Protocol v1. One newline-delimited frame is "@R1 " plus one flat JSON
// object. Diagnostic logs use the "WLS " prefix, so a frame is distinct from a
// log line and from the test-only "WLS-HIL " commands.

static constexpr int kRemoteVersion = 1;
static constexpr int kRemoteMaxLine = 320;
static constexpr char kRemotePrefix[] = "@R1 ";

enum class RemoteLink : uint8_t { Disconnected = 0, ConnectedUsb = 1 };

struct RemoteSession {
  RemoteLink link = RemoteLink::Disconnected;
  bool streaming = false;
  // 0 is GET_RESULTS. 1 is GET_SERVICES. Existing zero-init stays on results.
  uint8_t streamKind = 0;
  uint16_t rowCursor = 0;
  uint16_t rowCount = 0;
  uint16_t rejects = 0;
};

// One host's tested ports. ports uses port=o;port=c;port=t;port=e.
struct ServiceWireRow {
  char ip[16] = {};
  char ports[160] = {};
  uint16_t openCount = 0;
};

struct RemoteServices {
  bool (*apply)(void* context, AppAction action, int rowOffset, const char* text) = nullptr;
  // Read only after apply returns false. True means the press ack is still showing.
  bool (*rejectedBusy)(void* context) = nullptr;
  void (*loadState)(void* context, AppState* out) = nullptr;
  int (*rowCount)(void* context) = nullptr;
  bool (*rowAt)(void* context, int index, InventoryRow* out) = nullptr;
  bool (*serviceAt)(void* context, int index, ServiceWireRow* out) = nullptr;
  void* context = nullptr;
};

// Handles one inbound line. Writes at most one reply frame, including the
// prefix and newline. Returns the reply length, or 0 when there is no reply.
int remoteSubmit(RemoteSession* session, const char* line, char* out, int outCap, const RemoteServices* services);

// Next GET_RESULTS frame. Returns 0 when the session is not streaming.
int remotePull(RemoteSession* session, char* out, int outCap, const RemoteServices* services);

// A line that exceeded kRemoteMaxLine was discarded through the newline.
int remoteMarkOversize(RemoteSession* session, char* out, int outCap);
