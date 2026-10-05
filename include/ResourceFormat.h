#pragma once

#include <stdint.h>

// Low-rate fuel gauge. The numbers are injected in host tests.
// The device reader lives in ResourceMeter.cpp and is not part of the native image.

struct ResourceSample {
  uint32_t heap = 0;
  uint32_t minHeap = 0;
  uint32_t maxBlock = 0;
  uint32_t psram = 0;
  uint32_t freePsram = 0;
  uint32_t minPsram = 0;
};

// WLS resource phase=<token> heap= min= block= psram= freePsram= minPsram=
// Returns the length, or -1 when the buffer is too small.
int formatResourceLine(char* out, int cap, const char* phase, const ResourceSample& sample);
