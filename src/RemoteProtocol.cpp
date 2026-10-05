#include "RemoteProtocol.h"

#include <stdio.h>
#include <string.h>

namespace {

struct Buf {
  char* data;
  int cap;
  int used;
  bool ok;
};

void addRaw(Buf& buf, const char* text) {
  if (!buf.ok || text == nullptr) {
    return;
  }
  while (*text != '\0') {
    if (buf.used + 1 >= buf.cap) {
      buf.ok = false;
      if (buf.cap > 0) {
        buf.data[0] = '\0';
      }
      return;
    }
    buf.data[buf.used++] = *text++;
  }
  buf.data[buf.used] = '\0';
}

void addEsc(Buf& buf, const char* text) {
  if (text == nullptr) {
    return;
  }
  while (*text != '\0') {
    const unsigned char c = static_cast<unsigned char>(*text++);
    if (c < 32 || c == 127) {
      continue;
    }
    if (c == '"' || c == '\\') {
      addRaw(buf, "\\");
    }
    char one[2] = {static_cast<char>(c), '\0'};
    addRaw(buf, one);
  }
}

void addInt(Buf& buf, int value) {
  char tmp[16];
  snprintf(tmp, sizeof(tmp), "%d", value);
  addRaw(buf, tmp);
}

int finishFrame(char* out, int outCap, const char* body) {
  if (out == nullptr || outCap < 8 || body == nullptr) {
    return 0;
  }
  const int n = snprintf(out, static_cast<size_t>(outCap), "@R1 %s\n", body);
  if (n < 0 || n >= outCap) {
    out[0] = '\0';
    return 0;
  }
  return n;
}

int writeErr(char* out, int outCap, const char* reason) {
  char body[64];
  snprintf(body, sizeof(body), "{\"v\":1,\"op\":\"ERR\",\"reason\":\"%s\"}", reason);
  return finishFrame(out, outCap, body);
}

const char* skipSpace(const char* p) {
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
    ++p;
  }
  return p;
}

bool parseString(const char** cursor, char* dest, size_t cap) {
  const char* p = *cursor;
  if (*p != '"') {
    return false;
  }
  ++p;
  size_t n = 0;
  while (*p != '\0' && *p != '"') {
    char c = *p++;
    if (c == '\\') {
      if (*p != '"' && *p != '\\') {
        return false;
      }
      c = *p++;
    }
    if (static_cast<unsigned char>(c) < 32) {
      return false;
    }
    if (n + 1 >= cap) {
      return false;
    }
    if (dest != nullptr) {
      dest[n] = c;
    }
    ++n;
  }
  if (*p != '"') {
    return false;
  }
  if (dest != nullptr) {
    dest[n] = '\0';
  }
  *cursor = p + 1;
  return true;
}

bool parseNumber(const char** cursor, int* value) {
  const char* p = *cursor;
  int sign = 1;
  if (*p == '-') {
    sign = -1;
    ++p;
  }
  if (*p < '0' || *p > '9') {
    return false;
  }
  long acc = 0;
  while (*p >= '0' && *p <= '9') {
    acc = acc * 10 + (*p - '0');
    if (acc > 1000000L) {
      return false;
    }
    ++p;
  }
  if (value != nullptr) {
    *value = static_cast<int>(acc) * sign;
  }
  *cursor = p;
  return true;
}

bool flatObject(const char* json) {
  const char* p = skipSpace(json);
  if (*p != '{') {
    return false;
  }
  ++p;
  p = skipSpace(p);
  if (*p == '}') {
    return skipSpace(p + 1)[0] == '\0';
  }
  while (*p != '\0') {
    char key[24];
    if (!parseString(&p, key, sizeof(key))) {
      return false;
    }
    p = skipSpace(p);
    if (*p != ':') {
      return false;
    }
    ++p;
    p = skipSpace(p);
    if (*p == '"') {
      if (!parseString(&p, nullptr, 96)) {
        return false;
      }
    } else if (!parseNumber(&p, nullptr)) {
      return false;
    }
    p = skipSpace(p);
    if (*p == ',') {
      ++p;
      p = skipSpace(p);
      continue;
    }
    if (*p == '}') {
      return skipSpace(p + 1)[0] == '\0';
    }
    return false;
  }
  return false;
}

bool findString(const char* json, const char* key, char* dest, size_t cap) {
  const char* p = skipSpace(json);
  if (*p != '{') {
    return false;
  }
  ++p;
  while (*p != '\0' && *p != '}') {
    p = skipSpace(p);
    char found[24];
    if (!parseString(&p, found, sizeof(found))) {
      return false;
    }
    p = skipSpace(p);
    if (*p != ':') {
      return false;
    }
    ++p;
    p = skipSpace(p);
    const bool match = strcmp(found, key) == 0;
    if (*p == '"') {
      if (match) {
        return parseString(&p, dest, cap);
      }
      if (!parseString(&p, nullptr, 96)) {
        return false;
      }
    } else if (!parseNumber(&p, nullptr)) {
      return false;
    }
    p = skipSpace(p);
    if (*p == ',') {
      ++p;
    }
  }
  return false;
}

bool findInt(const char* json, const char* key, int* value) {
  const char* p = skipSpace(json);
  if (*p != '{') {
    return false;
  }
  ++p;
  while (*p != '\0' && *p != '}') {
    p = skipSpace(p);
    char found[24];
    if (!parseString(&p, found, sizeof(found))) {
      return false;
    }
    p = skipSpace(p);
    if (*p != ':') {
      return false;
    }
    ++p;
    p = skipSpace(p);
    const bool match = strcmp(found, key) == 0;
    if (*p == '"') {
      if (!parseString(&p, nullptr, 96)) {
        return false;
      }
    } else {
      int number = 0;
      if (!parseNumber(&p, &number)) {
        return false;
      }
      if (match && value != nullptr) {
        *value = number;
        return true;
      }
    }
    p = skipSpace(p);
    if (*p == ',') {
      ++p;
    }
  }
  return false;
}

const char* jsonOf(const char* line) {
  if (strncmp(line, "@R1 ", 4) == 0) {
    return line + 4;
  }
  return line;
}

struct NamedAction {
  const char* name;
  AppAction action;
  bool row;
};

const NamedAction kActions[] = {
    {"find", AppAction::FindNetworks, false}, {"forget", AppAction::ForgetNetwork, false},
    {"start", AppAction::StartScan, false},   {"pause", AppAction::PauseScan, false},
    {"resume", AppAction::ResumeScan, false}, {"stop", AppAction::StopScan, false},
    {"reset", AppAction::ResetScan, false},   {"hosts", AppAction::OpenHosts, false},
    {"back", AppAction::Back, false},         {"next", AppAction::NextPage, false},
    {"prev", AppAction::PrevPage, false},     {"shift", AppAction::Shift, false},
    {"page", AppAction::KeyboardPage, false}, {"del", AppAction::Backspace, false},
    {"ok", AppAction::SubmitPassword, false}, {"close", AppAction::CancelPassword, false},
    {"row", AppAction::SelectRow, true},
};

const NamedAction* findAction(const char* name) {
  for (size_t i = 0; i < sizeof(kActions) / sizeof(kActions[0]); ++i) {
    if (strcmp(kActions[i].name, name) == 0) {
      return &kActions[i];
    }
  }
  return nullptr;
}

const char* screenToken(AppScreen screen) {
  switch (screen) {
    case AppScreen::Results:
      return "results";
    case AppScreen::Entry:
      return "entry";
    case AppScreen::Hosts:
      return "hosts";
    case AppScreen::Home:
      return "home";
  }
  return "home";
}

int writeState(char* out, int outCap, const AppState& state) {
  char body[512];
  Buf buf{body, static_cast<int>(sizeof(body)), 0, true};
  addRaw(buf, "{\"v\":1,\"op\":\"STATE\",\"screen\":\"");
  addEsc(buf, screenToken(state.screen));
  addRaw(buf, "\",\"wifi\":\"");
  addEsc(buf, state.wifiPhase);
  addRaw(buf, "\",\"ssid\":\"");
  addEsc(buf, state.ssid);
  addRaw(buf, "\",\"saved\":");
  addInt(buf, state.saved ? 1 : 0);
  addRaw(buf, ",\"scan\":\"");
  addEsc(buf, state.scan);
  addRaw(buf, "\",\"processed\":");
  addInt(buf, state.processed);
  addRaw(buf, ",\"candidates\":");
  addInt(buf, state.candidates);
  addRaw(buf, ",\"observed\":");
  addInt(buf, state.observed);
  addRaw(buf, ",\"current\":\"");
  addEsc(buf, state.current);
  addRaw(buf, "\",\"last\":\"");
  addEsc(buf, state.last);
  addRaw(buf, "\",\"newest\":\"");
  addEsc(buf, state.newest);
  addRaw(buf, "\",\"elapsed\":");
  addInt(buf, static_cast<int>(state.elapsedMs));
  addRaw(buf, ",\"hosts\":");
  addInt(buf, state.hostsOpen ? 1 : 0);
  addRaw(buf, ",\"page\":");
  addInt(buf, state.page);
  addRaw(buf, ",\"canStart\":");
  addInt(buf, state.canStart ? 1 : 0);
  addRaw(buf, ",\"canPause\":");
  addInt(buf, state.canPause ? 1 : 0);
  addRaw(buf, ",\"canResume\":");
  addInt(buf, state.canResume ? 1 : 0);
  addRaw(buf, "}");
  if (!buf.ok) {
    return writeErr(out, outCap, "state");
  }
  return finishFrame(out, outCap, body);
}

int writeRow(char* out, int outCap, int index, const InventoryRow& row) {
  char body[512];
  Buf buf{body, static_cast<int>(sizeof(body)), 0, true};
  addRaw(buf, "{\"v\":1,\"op\":\"RESULT_ROW\",\"i\":");
  addInt(buf, index);
  addRaw(buf, ",\"ip\":\"");
  addEsc(buf, row.ip);
  addRaw(buf, "\",\"mac\":\"");
  addEsc(buf, row.mac);
  addRaw(buf, "\",\"method\":\"");
  addEsc(buf, row.method);
  addRaw(buf, "\",\"name\":\"");
  addEsc(buf, row.name);
  addRaw(buf, "\",\"nameSource\":\"");
  addEsc(buf, row.nameSource);
  addRaw(buf, "\",\"macClass\":\"");
  addEsc(buf, row.macClass);
  addRaw(buf, "\",\"ouiState\":\"");
  addEsc(buf, row.ouiState);
  addRaw(buf, "\",\"manufacturer\":\"");
  addEsc(buf, row.manufacturer);
  addRaw(buf, "\"}");
  if (!buf.ok) {
    return writeErr(out, outCap, "row");
  }
  return finishFrame(out, outCap, body);
}

int writeEnd(char* out, int outCap, int count) {
  char body[64];
  snprintf(body, sizeof(body), "{\"v\":1,\"op\":\"RESULT_END\",\"count\":%d}", count);
  return finishFrame(out, outCap, body);
}

}  // namespace

int remoteMarkOversize(RemoteSession* session, char* out, int outCap) {
  if (session == nullptr) {
    return 0;
  }
  session->rejects = static_cast<uint16_t>(session->rejects + 1);
  session->streaming = false;
  return writeErr(out, outCap, "oversize");
}

int remotePull(RemoteSession* session, char* out, int outCap, const RemoteServices* services) {
  if (session == nullptr || !session->streaming) {
    return 0;
  }
  if (services == nullptr || services->rowAt == nullptr) {
    session->streaming = false;
    return writeErr(out, outCap, "results");
  }
  if (session->rowCursor >= session->rowCount) {
    session->streaming = false;
    return writeEnd(out, outCap, session->rowCount);
  }
  InventoryRow row;
  if (!services->rowAt(services->context, session->rowCursor, &row)) {
    session->streaming = false;
    return writeErr(out, outCap, "results");
  }
  const int index = session->rowCursor;
  session->rowCursor = static_cast<uint16_t>(session->rowCursor + 1);
  return writeRow(out, outCap, index, row);
}

int remoteSubmit(RemoteSession* session, const char* line, char* out, int outCap, const RemoteServices* services) {
  if (session == nullptr || line == nullptr || out == nullptr || outCap < 16) {
    return 0;
  }
  out[0] = '\0';
  if (strlen(line) > static_cast<size_t>(kRemoteMaxLine)) {
    return remoteMarkOversize(session, out, outCap);
  }
  if (session->streaming) {
    return writeErr(out, outCap, "busy");
  }
  const char* json = skipSpace(jsonOf(line));
  if (!flatObject(json)) {
    session->rejects = static_cast<uint16_t>(session->rejects + 1);
    return writeErr(out, outCap, "malformed");
  }
  int version = -1;
  char op[16];
  if (!findInt(json, "v", &version) || !findString(json, "op", op, sizeof(op))) {
    session->rejects = static_cast<uint16_t>(session->rejects + 1);
    return writeErr(out, outCap, "malformed");
  }
  if (strcmp(op, "HELLO") == 0) {
    if (session->link == RemoteLink::ConnectedUsb) {
      return writeErr(out, outCap, "session");
    }
    if (version != kRemoteVersion) {
      return finishFrame(out, outCap, "{\"v\":1,\"op\":\"HELLO_ACK\",\"ok\":0,\"link\":\"none\",\"support\":1}");
    }
    session->link = RemoteLink::ConnectedUsb;
    session->rejects = 0;
    return finishFrame(out, outCap, "{\"v\":1,\"op\":\"HELLO_ACK\",\"ok\":1,\"link\":\"usb\",\"support\":1}");
  }
  if (session->link != RemoteLink::ConnectedUsb) {
    return writeErr(out, outCap, "closed");
  }
  if (version != kRemoteVersion) {
    return writeErr(out, outCap, "version");
  }
  if (strcmp(op, "GOODBYE") == 0) {
    session->link = RemoteLink::Disconnected;
    session->streaming = false;
    return finishFrame(out, outCap, "{\"v\":1,\"op\":\"GOODBYE\",\"ok\":1}");
  }
  if (strcmp(op, "PING") == 0) {
    int id = 0;
    findInt(json, "id", &id);
    char body[48];
    snprintf(body, sizeof(body), "{\"v\":1,\"op\":\"PONG\",\"id\":%d}", id);
    return finishFrame(out, outCap, body);
  }
  if (strcmp(op, "GET_STATE") == 0) {
    if (services == nullptr || services->loadState == nullptr) {
      return writeErr(out, outCap, "state");
    }
    AppState state;
    services->loadState(services->context, &state);
    return writeState(out, outCap, state);
  }
  if (strcmp(op, "ACTION") == 0) {
    char name[16];
    if (!findString(json, "name", name, sizeof(name))) {
      return writeErr(out, outCap, "malformed");
    }
    const NamedAction* named = findAction(name);
    if (named == nullptr) {
      return writeErr(out, outCap, "unknown");
    }
    int index = -1;
    const bool hasIndex = findInt(json, "index", &index);
    if (named->row && (!hasIndex || index < 0 || index > 5)) {
      return writeErr(out, outCap, "range");
    }
    if (services == nullptr || services->apply == nullptr ||
        !services->apply(services->context, named->action, named->row ? index : -1)) {
      char body[80];
      snprintf(body, sizeof(body), "{\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"%s\",\"ok\":0}", name);
      return finishFrame(out, outCap, body);
    }
    char body[80];
    snprintf(body, sizeof(body), "{\"v\":1,\"op\":\"ACTION_RESULT\",\"name\":\"%s\",\"ok\":1}", name);
    return finishFrame(out, outCap, body);
  }
  if (strcmp(op, "GET_RESULTS") == 0) {
    if (services == nullptr || services->rowCount == nullptr) {
      return writeErr(out, outCap, "results");
    }
    const int count = services->rowCount(services->context);
    if (count < 0 || count > 256) {
      return writeErr(out, outCap, "results");
    }
    session->rowCount = static_cast<uint16_t>(count);
    session->rowCursor = 0;
    if (count == 0) {
      session->streaming = false;
      return writeEnd(out, outCap, 0);
    }
    session->streaming = true;
    return remotePull(session, out, outCap, services);
  }
  return writeErr(out, outCap, "unknown");
}
