#include "ServiceResultView.h"

#include <stdio.h>
#include <string.h>

namespace {

bool copyText(char* dest, size_t cap, const char* text) {
  if (dest == nullptr || cap == 0 || text == nullptr) {
    return false;
  }
  const size_t n = strlen(text);
  if (n + 1 > cap) {
    dest[0] = '\0';
    return false;
  }
  memcpy(dest, text, n + 1);
  return true;
}

bool appendText(char* dest, size_t cap, const char* extra) {
  if (dest == nullptr || cap == 0 || extra == nullptr) {
    return false;
  }
  const size_t have = strlen(dest);
  const size_t add = strlen(extra);
  if (have + add + 1 > cap) {
    return false;
  }
  memcpy(dest + have, extra, add + 1);
  return true;
}

void upperFamily(char* dest, size_t cap, const char* family) {
  if (dest == nullptr || cap == 0) {
    return;
  }
  size_t n = 0;
  if (family != nullptr) {
    for (; family[n] != '\0' && n + 1 < cap; ++n) {
      unsigned char c = static_cast<unsigned char>(family[n]);
      if (c >= 'a' && c <= 'z') {
        c = static_cast<unsigned char>(c - 'a' + 'A');
      }
      dest[n] = static_cast<char>(c);
    }
  }
  dest[n] = '\0';
}

int ipOrder(const Ipv4& left, const Ipv4& right) {
  for (int i = 0; i < 4; ++i) {
    if (left.octet[i] < right.octet[i]) {
      return -1;
    }
    if (left.octet[i] > right.octet[i]) {
      return 1;
    }
  }
  return 0;
}

}  // namespace

const char* serviceStateWord(ServiceProbeClass state) {
  switch (state) {
    case ServiceProbeClass::Open:
      return "OPEN";
    case ServiceProbeClass::Closed:
      return "CLOSED";
    case ServiceProbeClass::Timeout:
      return "TIMEOUT";
    case ServiceProbeClass::Error:
      return "ERROR";
    case ServiceProbeClass::None:
      return "UNTESTED";
  }
  return "UNTESTED";
}

bool serviceHostHasOpen(const ServiceHostResult* result) {
  return result != nullptr && result->tested > 0 && result->openCount > 0;
}

bool formatServicePortLabel(char* out, size_t cap, uint16_t port, const char* family) {
  char name[16];
  upperFamily(name, sizeof(name), family != nullptr && family[0] != '\0' ? family : "port");
  if (out == nullptr || cap < 4) {
    return false;
  }
  const int written = snprintf(out, cap, "%u %s", static_cast<unsigned>(port), name);
  return written > 0 && static_cast<size_t>(written) < cap;
}

bool formatServiceSummary(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result) {
  if (out == nullptr || cap < 2) {
    return false;
  }
  out[0] = '\0';
  if (result == nullptr || result->tested == 0) {
    return copyText(out, cap, "Not scanned");
  }
  if (result->openCount == 0) {
    return copyText(out, cap, "Open: none");
  }
  if (!copyText(out, cap, "Open:")) {
    return false;
  }
  const int ports = serviceProfilePortCount(profile);
  int limit = result->tested;
  if (limit > ports) {
    limit = ports;
  }
  if (limit > kServicePortCap) {
    limit = kServicePortCap;
  }
  int shown = 0;
  for (int i = 0; i < limit; ++i) {
    if (result->state[i] != ServiceProbeClass::Open) {
      continue;
    }
    char name[16];
    char token[28];
    const char* family = serviceProfilePortFamily(profile, static_cast<uint8_t>(i));
    upperFamily(name, sizeof(name), family != nullptr && family[0] != '\0' ? family : "port");
    const uint16_t port = serviceProfilePort(profile, static_cast<uint8_t>(i));
    if (shown == 0) {
      snprintf(token, sizeof(token), " %u %s", static_cast<unsigned>(port), name);
    } else {
      snprintf(token, sizeof(token), ", %u %s", static_cast<unsigned>(port), name);
    }
    if (!appendText(out, cap, token)) {
      break;
    }
    ++shown;
  }
  if (result->openCount > shown) {
    char more[8];
    snprintf(more, sizeof(more), " +%u", static_cast<unsigned>(result->openCount - shown));
    appendText(out, cap, more);
  }
  return out[0] != '\0';
}

int buildServiceHostView(uint16_t* out, int cap, int hostCount, const Ipv4* ips, ServiceResultLookup resultAt,
                         void* context, bool openOnly) {
  if (out == nullptr || ips == nullptr || cap <= 0 || hostCount <= 0) {
    return 0;
  }
  int count = 0;
  const int limit = hostCount;
  for (int index = 0; index < limit; ++index) {
    const ServiceHostResult* result = resultAt != nullptr ? resultAt(context, static_cast<uint16_t>(index)) : nullptr;
    if (openOnly && !serviceHostHasOpen(result)) {
      continue;
    }
    if (count >= cap) {
      break;
    }
    int slot = count;
    while (slot > 0 && ipOrder(ips[index], ips[out[slot - 1]]) < 0) {
      out[slot] = out[slot - 1];
      --slot;
    }
    out[slot] = static_cast<uint16_t>(index);
    ++count;
  }
  return count;
}
