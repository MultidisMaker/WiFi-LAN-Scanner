#include "ResourceFormat.h"

#include <stdio.h>

namespace {

void copyPhase(char* dest, int cap, const char* phase) {
  int n = 0;
  if (dest == nullptr || cap < 2) {
    return;
  }
  if (phase != nullptr) {
    for (int i = 0; phase[i] != '\0' && n + 1 < cap; ++i) {
      const char c = phase[i];
      const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
      if (ok) {
        dest[n++] = c;
      }
    }
  }
  if (n == 0) {
    dest[0] = 'x';
    dest[1] = '\0';
    return;
  }
  dest[n] = '\0';
}

}  // namespace

int formatResourceLine(char* out, int cap, const char* phase, const ResourceSample& sample) {
  if (out == nullptr || cap < 16) {
    return -1;
  }
  char token[24];
  copyPhase(token, static_cast<int>(sizeof(token)), phase);
  const int n = snprintf(out, static_cast<size_t>(cap),
                         "WLS resource phase=%s heap=%lu min=%lu block=%lu psram=%lu freePsram=%lu minPsram=%lu", token,
                         static_cast<unsigned long>(sample.heap), static_cast<unsigned long>(sample.minHeap),
                         static_cast<unsigned long>(sample.maxBlock), static_cast<unsigned long>(sample.psram),
                         static_cast<unsigned long>(sample.freePsram), static_cast<unsigned long>(sample.minPsram));
  if (n < 0 || n >= cap) {
    out[0] = '\0';
    return -1;
  }
  return n;
}
