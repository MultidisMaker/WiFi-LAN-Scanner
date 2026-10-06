#include "AddressRange.h"

#include <stdio.h>
#include <string.h>

namespace {

uint32_t pack(const Ipv4& ip) {
  return (uint32_t(ip.octet[0]) << 24) | (uint32_t(ip.octet[1]) << 16) | (uint32_t(ip.octet[2]) << 8) |
         uint32_t(ip.octet[3]);
}

Ipv4 unpack(uint32_t value) {
  return ipv4(static_cast<uint8_t>((value >> 24) & 0xFF), static_cast<uint8_t>((value >> 16) & 0xFF),
              static_cast<uint8_t>((value >> 8) & 0xFF), static_cast<uint8_t>(value & 0xFF));
}

void copyReason(RangePreview& preview, const char* reason) {
  size_t i = 0;
  if (reason != nullptr) {
    for (; reason[i] != '\0' && i + 1 < sizeof(preview.reason); ++i) {
      preview.reason[i] = reason[i];
    }
  }
  preview.reason[i] = '\0';
}

void sortPlan(CandidatePlan& plan) {
  for (uint16_t i = 1; i < plan.count; ++i) {
    const Ipv4 item = plan.address[i];
    const uint32_t packed = pack(item);
    uint16_t j = i;
    while (j > 0 && pack(plan.address[j - 1]) > packed) {
      plan.address[j] = plan.address[j - 1];
      --j;
    }
    plan.address[j] = item;
  }
}

bool eligible(uint32_t address, uint32_t network, uint32_t broadcast, uint32_t self) {
  return address > network && address < broadcast && address != self;
}

bool hasEligibleFrom(uint32_t origin, uint32_t network, uint32_t broadcast, uint32_t self) {
  if (origin >= broadcast) {
    return false;
  }
  uint32_t cursor = origin <= network ? network + 1 : origin;
  while (cursor < broadcast) {
    if (cursor != self) {
      return true;
    }
    if (cursor == 0xFFFFFFFEUL) {
      break;
    }
    ++cursor;
  }
  return false;
}

uint32_t firstEligible(uint32_t network, uint32_t broadcast, uint32_t self) {
  uint32_t cursor = network + 1;
  if (cursor == self && cursor + 1 < broadcast) {
    ++cursor;
  }
  return cursor;
}

void markEnds(CandidatePlan& plan, RangePreview& preview) {
  if (plan.count == 0) {
    return;
  }
  uint32_t low = pack(plan.address[0]);
  uint32_t high = low;
  for (uint16_t i = 1; i < plan.count; ++i) {
    const uint32_t packed = pack(plan.address[i]);
    if (packed < low) {
      low = packed;
    }
    if (packed > high) {
      high = packed;
    }
  }
  preview.start = unpack(low);
  preview.end = unpack(high);
  preview.count = plan.count;
  preview.valid = plan.count > 0 && plan.count <= kCandidateCap;
  plan.valid = preview.valid;
}

}  // namespace

bool addressLimitOk(uint16_t limit) { return limit == 64 || limit == 128 || limit == 256; }

bool parseIpv4(const char* text, Ipv4& out) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  unsigned parts[4] = {};
  int index = 0;
  int digits = 0;
  for (const char* p = text;; ++p) {
    const char c = *p;
    if (c >= '0' && c <= '9') {
      if (digits == 3) {
        return false;
      }
      parts[index] = parts[index] * 10u + static_cast<unsigned>(c - '0');
      if (parts[index] > 255u) {
        return false;
      }
      ++digits;
      continue;
    }
    if (digits == 0) {
      return false;
    }
    if (c == '.' && index < 3) {
      ++index;
      digits = 0;
      continue;
    }
    if (c == '\0' && index == 3) {
      out = ipv4(static_cast<uint8_t>(parts[0]), static_cast<uint8_t>(parts[1]), static_cast<uint8_t>(parts[2]),
                 static_cast<uint8_t>(parts[3]));
      return true;
    }
    return false;
  }
}

void previewAddressRange(const NetFacts& facts, const AddressWindow& window, CandidatePlan& plan, RangePreview& preview) {
  plan = CandidatePlan();
  preview = RangePreview();
  preview.mode = window.mode;
  preview.limit = window.limit;
  if (!addressLimitOk(window.limit)) {
    copyReason(preview, "limit");
    return;
  }
  if (!facts.valid || facts.usableHosts == 0) {
    copyReason(preview, "offline");
    return;
  }
  const uint32_t network = pack(facts.network);
  const uint32_t broadcast = pack(facts.broadcast);
  const uint32_t self = pack(facts.address);
  const uint32_t gateway = pack(facts.gateway);
  if (broadcast <= network + 1) {
    copyReason(preview, "offline");
    return;
  }

  const bool automatic = window.mode == RangeMode::Automatic && !window.useOrigin;
  if (automatic) {
    uint32_t eligibleCount = facts.usableHosts;
    if (self > network && self < broadcast && eligibleCount > 0) {
      --eligibleCount;
    }
    Ipv4 naturalEnd = {};
    for (uint32_t cursor = network + 1; cursor < broadcast && plan.count < window.limit; ++cursor) {
      if (cursor == self) {
        continue;
      }
      plan.address[plan.count++] = unpack(cursor);
      naturalEnd = plan.address[plan.count - 1];
      if (cursor == 0xFFFFFFFEUL) {
        break;
      }
    }
    plan.eligibleCount = eligibleCount;
    plan.capped = eligibleCount > window.limit;
    const bool gatewayEligible = eligible(gateway, network, broadcast, self);
    bool present = false;
    if (gatewayEligible) {
      for (uint16_t i = 0; i < plan.count; ++i) {
        if (pack(plan.address[i]) == gateway) {
          present = true;
          break;
        }
      }
      if (!present && plan.capped && plan.count > 0) {
        preview.nextOrigin = plan.address[plan.count - 1];
        plan.address[plan.count - 1] = unpack(gateway);
        plan.gatewayForced = true;
        sortPlan(plan);
        present = true;
      }
      plan.gatewayIncluded = present;
    }
    preview.gatewayForced = plan.gatewayForced;
    preview.gatewayIncluded = plan.gatewayIncluded;
    if (!plan.gatewayForced && plan.count > 0) {
      const uint32_t next = pack(naturalEnd) + 1;
      preview.nextOrigin = unpack(next);
    }
    preview.canNext = hasEligibleFrom(pack(preview.nextOrigin), network, broadcast, self);
    preview.canPrev = false;
    if (plan.count == 0) {
      copyReason(preview, "empty");
      return;
    }
    markEnds(plan, preview);
    return;
  }

  const uint32_t origin = window.useOrigin ? pack(window.origin) : network + 1;
  if (origin <= network || origin >= broadcast) {
    copyReason(preview, "outside");
    return;
  }
  for (uint32_t cursor = origin; cursor < broadcast && plan.count < window.limit; ++cursor) {
    if (cursor == self) {
      continue;
    }
    plan.address[plan.count++] = unpack(cursor);
    if (cursor == 0xFFFFFFFEUL) {
      break;
    }
  }
  if (plan.count == 0) {
    copyReason(preview, "empty");
    return;
  }
  preview.clamped = plan.count < window.limit;
  for (uint16_t i = 0; i < plan.count; ++i) {
    if (pack(plan.address[i]) == gateway) {
      preview.gatewayIncluded = true;
      plan.gatewayIncluded = true;
      break;
    }
  }
  const uint32_t last = pack(plan.address[plan.count - 1]);
  preview.nextOrigin = unpack(last + 1);
  preview.canNext = hasEligibleFrom(last + 1, network, broadcast, self);
  const uint32_t first = firstEligible(network, broadcast, self);
  uint32_t cursor = origin;
  uint16_t walked = 0;
  uint32_t lowest = origin;
  while (cursor > network + 1 && walked < window.limit) {
    --cursor;
    if (!eligible(cursor, network, broadcast, self)) {
      continue;
    }
    lowest = cursor;
    ++walked;
  }
  preview.canPrev = window.mode == RangeMode::Custom;
  preview.prevAutomatic = walked > 0 && lowest == first;
  preview.prevOrigin = unpack(walked > 0 ? lowest : first);
  if (walked == 0) {
    preview.prevAutomatic = true;
  }
  markEnds(plan, preview);
}

int formatRangeMarker(char* out, size_t cap, const RangePreview& preview) {
  if (out == nullptr || cap < 16) {
    return -1;
  }
  int n = 0;
  if (!preview.valid) {
    n = snprintf(out, cap, "WLS range reject reason=%s", preview.reason[0] != '\0' ? preview.reason : "invalid");
  } else {
    char start[16];
    char end[16];
    formatIpv4(preview.start, start, sizeof(start));
    formatIpv4(preview.end, end, sizeof(end));
    n = snprintf(out, cap, "WLS range mode=%s start=%s end=%s count=%u limit=%u clamped=%d gateway=%d",
                 preview.mode == RangeMode::Custom ? "custom" : "automatic", start, end, preview.count, preview.limit,
                 preview.clamped ? 1 : 0, preview.gatewayIncluded ? 1 : 0);
  }
  if (n < 0 || static_cast<size_t>(n) >= cap) {
    out[0] = '\0';
    return -1;
  }
  return n;
}
