#include "NameRecord.h"

#include <stdio.h>
#include <string.h>

#include "NetMath.h"

namespace {

constexpr size_t kReadCap = 128;
constexpr size_t kStoredCap = 31;
constexpr size_t kDisplayCap = 13;

bool isNameChar(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
         c == '-';
}

char lowerAscii(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

bool endsWithLocal(const char* text, size_t length) {
  static const char kSuffix[] = ".local";
  if (length < sizeof(kSuffix) - 1) {
    return false;
  }
  const char* tail = text + (length - (sizeof(kSuffix) - 1));
  for (size_t i = 0; i < sizeof(kSuffix) - 1; ++i) {
    if (lowerAscii(tail[i]) != kSuffix[i]) {
      return false;
    }
  }
  return true;
}

int sourceRank(NameSource source) {
  if (source == NameSource::Mdns) {
    return 2;
  }
  if (source == NameSource::ReverseDns) {
    return 1;
  }
  return 0;
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

}  // namespace

bool sanitizeHostName(const char* input, char* out, size_t outLen) {
  if (out == nullptr || outLen < 2) {
    return false;
  }
  out[0] = '\0';
  if (input == nullptr) {
    return false;
  }

  char raw[kReadCap + 1];
  size_t length = 0;
  for (; input[length] != '\0' && length < kReadCap; ++length) {
    raw[length] = input[length];
  }
  raw[length] = '\0';
  while (length > 0 && (raw[length - 1] == '.' || raw[length - 1] == ' ')) {
    raw[--length] = '\0';
  }
  if (endsWithLocal(raw, length)) {
    length -= 6;
    raw[length] = '\0';
    while (length > 0 && raw[length - 1] == '.') {
      raw[--length] = '\0';
    }
  }

  const size_t storeCap = (outLen - 1) < kStoredCap ? (outLen - 1) : kStoredCap;
  size_t written = 0;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(raw[i]);
    if (!isNameChar(c)) {
      continue;
    }
    if (written < storeCap) {
      out[written++] = static_cast<char>(c);
    }
  }
  while (written > 0 && out[written - 1] == '.') {
    --written;
  }
  out[written] = '\0';
  return written > 0;
}

bool preferIncomingName(NameSource currentSource, const char* currentName, NameSource incoming,
                        const char* incomingSanitized) {
  if (incomingSanitized == nullptr || incomingSanitized[0] == '\0') {
    return false;
  }
  if (currentSource == NameSource::None || currentName == nullptr || currentName[0] == '\0') {
    return true;
  }
  const int incomingRank = sourceRank(incoming);
  const int currentRank = sourceRank(currentSource);
  if (incomingRank > currentRank) {
    return true;
  }
  if (incomingRank < currentRank) {
    return false;
  }
  return strcmp(incomingSanitized, currentName) < 0;
}

const char* nameSourceLabel(NameSource source) {
  if (source == NameSource::Mdns) {
    return "mdns";
  }
  if (source == NameSource::ReverseDns) {
    return "dns";
  }
  return "none";
}

void formatHostDetail(char* dest, size_t destLen, NameSource source, const char* storedName, bool hasMac,
                      const uint8_t mac[6]) {
  if (dest == nullptr || destLen == 0) {
    return;
  }
  char shown[kDisplayCap + 1];
  const char* use = "unknown";
  char tag = 'u';
  if (source != NameSource::None && storedName != nullptr && storedName[0] != '\0') {
    size_t i = 0;
    for (; i < kDisplayCap && storedName[i] != '\0'; ++i) {
      shown[i] = storedName[i];
    }
    shown[i] = '\0';
    use = shown;
    tag = source == NameSource::Mdns ? 'm' : 'd';
  }
  char macText[18];
  if (hasMac && mac != nullptr) {
    formatMac(mac, macText, sizeof(macText));
  } else {
    copyToken(macText, sizeof(macText), "MAC unknown");
  }
  snprintf(dest, destLen, "%c:%s %s", tag, use, macText);
}
