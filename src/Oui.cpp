#include "Oui.h"

#include <string.h>

namespace {

constexpr size_t kReadCap = 128;
constexpr size_t kStoredCap = 64;
constexpr size_t kDisplayCap = 31;

bool isHex(char c, uint32_t* nibble) {
  if (c >= '0' && c <= '9') {
    *nibble = static_cast<uint32_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    *nibble = static_cast<uint32_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    *nibble = static_cast<uint32_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

bool readOctet(const char* text, uint32_t* octet) {
  uint32_t hi = 0;
  uint32_t lo = 0;
  if (!isHex(text[0], &hi) || !isHex(text[1], &lo)) {
    return false;
  }
  *octet = (hi << 4) | lo;
  return true;
}

bool isManufacturerChar(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' ||
         c == '_' || c == '-' || c == '&' || c == '\'' || c == '(' || c == ')' || c == ',' || c == '/';
}

void copyBounded(char* dest, size_t destLen, const char* text) {
  if (destLen == 0) {
    return;
  }
  size_t limit = destLen - 1;
  if (limit > kDisplayCap) {
    limit = kDisplayCap;
  }
  size_t i = 0;
  if (text != nullptr) {
    for (; text[i] != '\0' && i < limit; ++i) {
      dest[i] = text[i];
    }
  }
  dest[i] = '\0';
}

const char* findOuiName(const OuiTable& table, uint32_t prefix) {
  uint32_t lo = 0;
  uint32_t hi = table.count;
  while (lo < hi) {
    const uint32_t mid = lo + ((hi - lo) / 2);
    const uint32_t key = table.entries[mid].prefix24;
    if (key < prefix) {
      lo = mid + 1;
    } else if (key > prefix) {
      hi = mid;
    } else {
      return table.names + table.entries[mid].nameOffset;
    }
  }
  return nullptr;
}

}  // namespace

MacClass classifyMac(const uint8_t mac[6]) {
  if (mac == nullptr) {
    return MacClass::Absent;
  }
  if ((mac[0] & 0x01u) != 0) {
    return MacClass::Group;
  }
  if ((mac[0] & 0x02u) != 0) {
    return MacClass::Local;
  }
  return MacClass::Global;
}

bool parseOuiAssignment(const char* text, uint32_t* prefix24) {
  if (text == nullptr || prefix24 == nullptr) {
    return false;
  }
  while (*text == ' ') {
    ++text;
  }
  char buf[16];
  size_t n = 0;
  while (text[n] != '\0') {
    if (n + 1 >= sizeof(buf)) {
      return false;
    }
    buf[n] = text[n];
    ++n;
  }
  while (n > 0 && buf[n - 1] == ' ') {
    --n;
  }
  buf[n] = '\0';
  uint32_t a = 0;
  uint32_t b = 0;
  uint32_t c = 0;
  if (n == 6) {
    if (!readOctet(buf, &a) || !readOctet(buf + 2, &b) || !readOctet(buf + 4, &c)) {
      return false;
    }
  } else if (n == 8 && buf[2] == ':' && buf[5] == ':') {
    if (!readOctet(buf, &a) || !readOctet(buf + 3, &b) || !readOctet(buf + 6, &c)) {
      return false;
    }
  } else if (n == 8 && buf[2] == '-' && buf[5] == '-') {
    if (!readOctet(buf, &a) || !readOctet(buf + 3, &b) || !readOctet(buf + 6, &c)) {
      return false;
    }
  } else {
    return false;
  }
  *prefix24 = (a << 16) | (b << 8) | c;
  return true;
}

bool sanitizeManufacturer(const char* raw, char* dest, size_t destLen) {
  if (dest == nullptr || destLen == 0) {
    return false;
  }
  dest[0] = '\0';
  if (raw == nullptr) {
    return false;
  }
  size_t stored = 0;
  size_t read = 0;
  for (size_t i = 0; raw[i] != '\0' && read < kReadCap; ++i) {
    ++read;
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (!isManufacturerChar(c)) {
      continue;
    }
    if (stored >= kStoredCap || stored + 1 >= destLen) {
      break;
    }
    dest[stored] = static_cast<char>(c);
    ++stored;
  }
  while (stored > 0 && (dest[stored - 1] == ' ' || dest[stored - 1] == '.')) {
    --stored;
  }
  size_t start = 0;
  while (start < stored && dest[start] == ' ') {
    ++start;
  }
  if (start > 0) {
    memmove(dest, dest + start, stored - start);
    stored -= start;
  }
  dest[stored] = '\0';
  return stored > 0;
}

const char* preferredOuiName(const char* current, const char* incoming) {
  if (incoming == nullptr || incoming[0] == '\0') {
    return current;
  }
  if (current == nullptr || current[0] == '\0') {
    return incoming;
  }
  if (strcmp(incoming, current) < 0) {
    return incoming;
  }
  return current;
}

OuiResult lookupOui(const OuiTable& table, const uint8_t mac[6]) {
  OuiResult result;
  result.macClass = classifyMac(mac);
  result.name = nullptr;
  if (mac == nullptr) {
    result.state = OuiState::None;
    return result;
  }
  if (result.macClass == MacClass::Group) {
    result.state = OuiState::Group;
    return result;
  }
  if (result.macClass == MacClass::Local) {
    result.state = OuiState::Local;
    return result;
  }
  const bool ready = table.entries != nullptr && table.names != nullptr && table.count > 0;
  if (!ready) {
    result.state = OuiState::DataMissing;
    return result;
  }
  const uint32_t prefix = (static_cast<uint32_t>(mac[0]) << 16) | (static_cast<uint32_t>(mac[1]) << 8) |
                          static_cast<uint32_t>(mac[2]);
  const char* found = findOuiName(table, prefix);
  if (found == nullptr || found[0] == '\0') {
    result.state = OuiState::Unknown;
    return result;
  }
  result.state = OuiState::Known;
  result.name = found;
  return result;
}

const char* macClassLabel(MacClass macClass) {
  switch (macClass) {
    case MacClass::Group:
      return "group";
    case MacClass::Local:
      return "local";
    case MacClass::Global:
      return "global";
    case MacClass::Absent:
      return "absent";
  }
  return "absent";
}

const char* ouiStateLabel(OuiState state) {
  switch (state) {
    case OuiState::Known:
      return "known";
    case OuiState::Unknown:
      return "unknown";
    case OuiState::Local:
      return "local";
    case OuiState::Group:
      return "group";
    case OuiState::DataMissing:
      return "unavailable";
    case OuiState::None:
      return "none";
    case OuiState::Unset:
      return "unset";
  }
  return "unset";
}

void formatOuiLine(char* dest, size_t destLen, OuiState state, const char* manufacturer) {
  if (dest == nullptr || destLen == 0) {
    return;
  }
  const char* text = "";
  switch (state) {
    case OuiState::Known:
      text = manufacturer != nullptr && manufacturer[0] != '\0' ? manufacturer : "unknown";
      break;
    case OuiState::Unknown:
      text = "unknown";
      break;
    case OuiState::Local:
      text = "local";
      break;
    case OuiState::Group:
      text = "group";
      break;
    case OuiState::DataMissing:
      text = "unavailable";
      break;
    case OuiState::None:
    case OuiState::Unset:
      text = "";
      break;
  }
  copyBounded(dest, destLen, text);
}
