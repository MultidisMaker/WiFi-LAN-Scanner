#include "DnsPtr.h"

#include <stdio.h>
#include <string.h>

namespace {

bool appendLabel(uint8_t* out, size_t cap, size_t* used, const char* text) {
  const size_t length = strlen(text);
  if (length == 0 || length > 63 || *used + 1 + length > cap) {
    return false;
  }
  out[(*used)++] = static_cast<uint8_t>(length);
  memcpy(out + *used, text, length);
  *used += length;
  return true;
}

size_t skipName(const uint8_t* msg, size_t len, size_t offset) {
  size_t hops = 0;
  while (offset < len && hops < 16) {
    const uint8_t mark = msg[offset];
    if (mark == 0) {
      return offset + 1;
    }
    if ((mark & 0xC0u) == 0xC0u) {
      return offset + 2 <= len ? offset + 2 : 0;
    }
    if ((mark & 0xC0u) != 0) {
      return 0;
    }
    offset += static_cast<size_t>(mark) + 1u;
    ++hops;
  }
  return 0;
}

bool decodeName(const uint8_t* msg, size_t len, size_t offset, char* name, size_t nameLen) {
  if (name == nullptr || nameLen < 2) {
    return false;
  }
  name[0] = '\0';
  size_t written = 0;
  size_t hops = 0;
  bool jumped = false;
  size_t cursor = offset;
  while (hops < 16 && cursor < len) {
    const uint8_t mark = msg[cursor];
    if (mark == 0) {
      return written > 0;
    }
    if ((mark & 0xC0u) == 0xC0u) {
      if (cursor + 1 >= len) {
        return false;
      }
      const size_t next = static_cast<size_t>(((mark & 0x3Fu) << 8) | msg[cursor + 1]);
      if (next >= len) {
        return false;
      }
      cursor = next;
      jumped = true;
      ++hops;
      continue;
    }
    if ((mark & 0xC0u) != 0 || cursor + 1u + mark > len) {
      return false;
    }
    if (written > 0) {
      if (written + 1 >= nameLen) {
        return false;
      }
      name[written++] = '.';
    }
    if (written + mark >= nameLen) {
      return false;
    }
    memcpy(name + written, msg + cursor + 1, mark);
    written += mark;
    name[written] = '\0';
    cursor += static_cast<size_t>(mark) + 1u;
    if (!jumped) {
      ++hops;
    }
  }
  return false;
}

}  // namespace

bool buildPtrQuestion(const Ipv4& ip, uint16_t id, uint8_t* out, size_t cap, size_t* used) {
  if (out == nullptr || used == nullptr || cap < 12) {
    return false;
  }
  memset(out, 0, cap);
  out[0] = static_cast<uint8_t>(id >> 8);
  out[1] = static_cast<uint8_t>(id & 0xFF);
  out[2] = 0x01;
  out[5] = 0x01;
  size_t cursor = 12;
  char label[4];
  for (int i = 3; i >= 0; --i) {
    snprintf(label, sizeof(label), "%u", static_cast<unsigned>(ip.octet[i]));
    if (!appendLabel(out, cap, &cursor, label)) {
      return false;
    }
  }
  if (!appendLabel(out, cap, &cursor, "in-addr") || !appendLabel(out, cap, &cursor, "arpa")) {
    return false;
  }
  if (cursor + 5 > cap) {
    return false;
  }
  out[cursor++] = 0;
  out[cursor++] = 0;
  out[cursor++] = 12;
  out[cursor++] = 0;
  out[cursor++] = 1;
  *used = cursor;
  return true;
}

bool parsePtrReply(const uint8_t* msg, size_t len, uint16_t id, char* name, size_t nameLen, PtrReply* reply) {
  if (msg == nullptr || reply == nullptr || len < 12) {
    return false;
  }
  const uint16_t seen = static_cast<uint16_t>((msg[0] << 8) | msg[1]);
  if (seen != id) {
    *reply = PtrReply::Malformed;
    return true;
  }
  const unsigned rcode = msg[3] & 0x0Fu;
  if (rcode == 3) {
    *reply = PtrReply::NxDomain;
    return true;
  }
  if (rcode != 0) {
    *reply = PtrReply::Malformed;
    return true;
  }
  const unsigned questions = static_cast<unsigned>((msg[4] << 8) | msg[5]);
  const unsigned answers = static_cast<unsigned>((msg[6] << 8) | msg[7]);
  size_t cursor = 12;
  for (unsigned i = 0; i < questions; ++i) {
    cursor = skipName(msg, len, cursor);
    if (cursor == 0 || cursor + 4 > len) {
      *reply = PtrReply::Malformed;
      return true;
    }
    cursor += 4;
  }
  if (answers == 0) {
    *reply = PtrReply::NoName;
    return true;
  }
  for (unsigned i = 0; i < answers; ++i) {
    cursor = skipName(msg, len, cursor);
    if (cursor == 0 || cursor + 10 > len) {
      *reply = PtrReply::Malformed;
      return true;
    }
    const unsigned type = static_cast<unsigned>((msg[cursor] << 8) | msg[cursor + 1]);
    const unsigned rdlen = static_cast<unsigned>((msg[cursor + 8] << 8) | msg[cursor + 9]);
    cursor += 10;
    if (cursor + rdlen > len) {
      *reply = PtrReply::Malformed;
      return true;
    }
    if (type == 12) {
      if (!decodeName(msg, len, cursor, name, nameLen)) {
        *reply = PtrReply::Malformed;
        return true;
      }
      *reply = PtrReply::Resolved;
      return true;
    }
    cursor += rdlen;
  }
  *reply = PtrReply::NoName;
  return true;
}

const char* ptrReplyLabel(PtrReply reply) {
  switch (reply) {
    case PtrReply::Resolved:
      return "resolved";
    case PtrReply::NxDomain:
      return "nxdomain";
    case PtrReply::NoName:
      return "noname";
    case PtrReply::Malformed:
      return "error";
  }
  return "error";
}
