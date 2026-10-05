#include "LwipArpBackend.h"

#include <string.h>

#include <lwip/etharp.h>
#include <lwip/ip4_addr.h>
#include <lwip/netif.h>

#include "BoardConfig.h"

namespace {
ip4_addr_t toAddr(const Ipv4& ip) {
  ip4_addr_t addr;
  IP4_ADDR(&addr, ip.octet[0], ip.octet[1], ip.octet[2], ip.octet[3]);
  return addr;
}

bool onStationSubnet(const struct netif* netif, const ip4_addr_t& target) {
  const ip4_addr_t* local = netif_ip4_addr(netif);
  const ip4_addr_t* mask = netif_ip4_netmask(netif);
  if (local == nullptr || mask == nullptr || ip4_addr_isany(mask)) {
    return false;
  }
  return (target.addr & mask->addr) == (local->addr & mask->addr);
}
}

void LwipArpBackend::reset() {
  active_ = false;
  blocked_ = false;
}

void LwipArpBackend::startProbe(const Ipv4& target, uint32_t nowMs) {
  active_ = true;
  blocked_ = false;
  target_ = target;
  startedMs_ = nowMs;
  struct netif* netif = netif_default;
  const ip4_addr_t addr = toAddr(target);
  if (netif == nullptr || !netif_is_up(netif) || !netif_is_link_up(netif) || !onStationSubnet(netif, addr)) {
    blocked_ = true;
    return;
  }
  const ip4_addr_t* local = netif_ip4_addr(netif);
  if (local != nullptr && local->addr == addr.addr) {
    blocked_ = true;
    return;
  }
  etharp_request(netif, &addr);
}

ProbeView LwipArpBackend::poll(uint32_t nowMs) {
  ProbeView view;
  view.method = methodName();
  if (!active_) {
    view.status = ProbeStatus::Idle;
    return view;
  }
  if (blocked_) {
    view.status = ProbeStatus::Unanswered;
    return view;
  }
  struct netif* netif = netif_default;
  const ip4_addr_t addr = toAddr(target_);
  struct eth_addr* eth = nullptr;
  const ip4_addr_t* foundIp = nullptr;
  if (netif != nullptr && etharp_find_addr(netif, &addr, &eth, &foundIp) >= 0 && eth != nullptr) {
    view.status = ProbeStatus::Observed;
    view.hasMac = true;
    memcpy(view.mac, eth->addr, 6);
    view.hasLatency = true;
    view.latencyMs = nowMs - startedMs_;
    view.evidence = EvidenceRank::Neighbor;
    return view;
  }
  if (nowMs - startedMs_ >= kArpProbeWaitMs) {
    view.status = ProbeStatus::Unanswered;
    return view;
  }
  view.status = ProbeStatus::Pending;
  return view;
}

void LwipArpBackend::cancel() { active_ = false; }

const char* LwipArpBackend::methodName() const { return "arp"; }
