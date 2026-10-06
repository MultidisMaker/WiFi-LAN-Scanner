#pragma once

#include <stddef.h>
#include <stdint.h>

#include "NetMath.h"
#include "ServiceScan.h"

// Card note and detail copy. These do not change probe classification.
// A null result, or tested == 0, is "Not scanned". Timeout stays its own word.

const char* serviceStateWord(ServiceProbeClass state);
bool serviceHostHasOpen(const ServiceHostResult* result);

// "22 SSH". The family is an uppercase inventory label.
bool formatServicePortLabel(char* out, size_t cap, uint16_t port, const char* family);

// Fits the existing 22-byte host-card note, including the NUL.
// Open names are added only while the whole token fits. The rest becomes " +N".
bool formatServiceSummary(char* out, size_t cap, ServiceProfile profile, const ServiceHostResult* result);

// Writes inventory indexes in IPv4 order. Does not reorder ips or results.
// openOnly keeps hosts with at least one Open probe. A null resultAt treats every host as untested.
typedef const ServiceHostResult* (*ServiceResultLookup)(void* context, uint16_t index);
int buildServiceHostView(uint16_t* out, int cap, int hostCount, const Ipv4* ips, ServiceResultLookup resultAt,
                         void* context, bool openOnly);
