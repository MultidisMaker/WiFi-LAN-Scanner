#include "ResourceMeter.h"

#include <Arduino.h>

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
