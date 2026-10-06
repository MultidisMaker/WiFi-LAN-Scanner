#pragma once

#include <stddef.h>
#include <stdint.h>

#include "NetMath.h"

// Observable result of one DNS PTR reply. Timeout and a missing resolver are
// transport outcomes and are not represented here.
enum class PtrReply : uint8_t { Resolved = 0, NxDomain = 1, NoName = 2, Malformed = 3 };

// Writes one recursive DNS query for the IPv4 PTR name. out receives the datagram.
bool buildPtrQuestion(const Ipv4& ip, uint16_t id, uint8_t* out, size_t cap, size_t* used);

// Reads one DNS response. name receives the first PTR target when reply is Resolved.
bool parsePtrReply(const uint8_t* msg, size_t len, uint16_t id, char* name, size_t nameLen, PtrReply* reply);

const char* ptrReplyLabel(PtrReply reply);
