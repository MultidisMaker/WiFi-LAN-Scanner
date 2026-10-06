#pragma once

#include <stddef.h>
#include <stdint.h>

// Higher rank wins. A reverse-DNS PTR outranks mDNS. mDNS stays a secondary source.
enum class NameSource : uint8_t { None = 0, ReverseDns = 1, Mdns = 2 };

enum class NameApply : uint8_t { MissingHost, Rejected, Kept, Applied };

// Copies a bounded display name into out. Returns false when the sanitized result is empty.
// Reads at most 128 input bytes, keeps letters, digits, '.', '_', and '-', stores at most 31,
// and strips one trailing ".local".
bool sanitizeHostName(const char* input, char* out, size_t outLen);

// True when the sanitized incoming name should replace the stored one.
bool preferIncomingName(NameSource currentSource, const char* currentName, NameSource incoming,
                        const char* incomingSanitized);

const char* nameSourceLabel(NameSource source);

// One size-1 detail line: "Name: <name-or-unknown> <MAC-or-MAC unknown>".
// The visible name is clipped so the MAC still fits the 210 px host card.
void formatHostDetail(char* dest, size_t destLen, NameSource source, const char* storedName, bool hasMac,
                      const uint8_t mac[6]);
