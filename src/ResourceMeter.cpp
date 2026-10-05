#include "ResourceMeter.h"

#include <Arduino.h>

#include "esp32-hal-psram.h"

#include "ResourceFormat.h"

void reportResource(const char* phase) {
  ResourceSample sample;
  sample.heap = ESP.getFreeHeap();
  sample.minHeap = ESP.getMinFreeHeap();
  sample.maxBlock = ESP.getMaxAllocHeap();
  sample.psram = ESP.getPsramSize();
  sample.freePsram = ESP.getFreePsram();
  sample.minPsram = ESP.getMinFreePsram();
  char line[180];
  if (formatResourceLine(line, static_cast<int>(sizeof(line)), phase, sample) > 0) {
    Serial.println(line);
  }
}

void reportPsramProbe() {
  const uint32_t size = ESP.getPsramSize();
  const uint32_t freeBytes = ESP.getFreePsram();
  uint8_t* block = static_cast<uint8_t*>(ps_malloc(64));
  bool wrote = false;
  if (block != nullptr) {
    block[0] = 0xA5;
    block[63] = 0x5A;
    wrote = block[0] == 0xA5 && block[63] == 0x5A;
    free(block);
  }
  const bool sizeOk = size >= (7u * 1024u * 1024u) && size <= (8u * 1024u * 1024u);
  const bool freeOk = freeBytes > 0 && freeBytes <= size;
  Serial.printf("WLS psram-alloc=%s size=%lu free=%lu\n", sizeOk && freeOk && wrote ? "ok" : "fail",
                static_cast<unsigned long>(size), static_cast<unsigned long>(freeBytes));
}
