#include "ServiceConnect.h"

#include <errno.h>
#include <fcntl.h>

#include <lwip/sockets.h>

namespace {

// ESP32 lwIP reports an active reject as ECONNREFUSED, ECONNRESET, or
// ECONNABORTED. Those are closed. A timeout stays in the unanswered set.
bool refused(int error) { return error == ECONNREFUSED || error == ECONNRESET || error == ECONNABORTED; }

bool unanswered(int error) {
  return error == ETIMEDOUT || error == EAGAIN || error == EWOULDBLOCK || error == EHOSTUNREACH || error == ENETUNREACH ||
         error == EHOSTDOWN || error == ENETDOWN;
}

}  // namespace

void WifiTcpConnect::closeSocket() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  waiting_ = false;
}

void WifiTcpConnect::start(const Ipv4& ip, uint16_t port, uint32_t nowMs, uint32_t timeoutMs) {
  closeSocket();
  held_ = ServiceConnectStatus::Pending;
  startedMs_ = nowMs;
  timeoutMs_ = timeoutMs == 0 ? 1 : timeoutMs;
  fd_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd_ < 0) {
    held_ = ServiceConnectStatus::Error;
    return;
  }
  const int flags = fcntl(fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    closeSocket();
    held_ = ServiceConnectStatus::Error;
    return;
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  const uint32_t host = (static_cast<uint32_t>(ip.octet[0]) << 24) | (static_cast<uint32_t>(ip.octet[1]) << 16) |
                        (static_cast<uint32_t>(ip.octet[2]) << 8) | static_cast<uint32_t>(ip.octet[3]);
  address.sin_addr.s_addr = htonl(host);
  const int connected = ::connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  if (connected == 0) {
    closeSocket();
    held_ = ServiceConnectStatus::Open;
    return;
  }
  if (errno == EINPROGRESS) {
    waiting_ = true;
    return;
  }
  const int error = errno;
  closeSocket();
  if (refused(error)) {
    held_ = ServiceConnectStatus::Closed;
  } else if (unanswered(error)) {
    held_ = ServiceConnectStatus::Timeout;
  } else {
    held_ = ServiceConnectStatus::Error;
  }
}

ServiceConnectStatus WifiTcpConnect::poll(uint32_t nowMs) {
  if (held_ != ServiceConnectStatus::Pending) {
    const ServiceConnectStatus status = held_;
    held_ = ServiceConnectStatus::Pending;
    return status;
  }
  if (!waiting_ || fd_ < 0) {
    closeSocket();
    return ServiceConnectStatus::Error;
  }
  fd_set writable;
  fd_set exceptional;
  FD_ZERO(&writable);
  FD_ZERO(&exceptional);
  FD_SET(fd_, &writable);
  FD_SET(fd_, &exceptional);
  timeval wait = {};
  const int ready = ::select(fd_ + 1, nullptr, &writable, &exceptional, &wait);
  if (ready > 0 && (FD_ISSET(fd_, &writable) || FD_ISSET(fd_, &exceptional))) {
    int soError = 0;
    socklen_t length = sizeof(soError);
    if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &soError, &length) < 0) {
      closeSocket();
      return ServiceConnectStatus::Error;
    }
    closeSocket();
    if (soError == 0) {
      return ServiceConnectStatus::Open;
    }
    if (refused(soError)) {
      return ServiceConnectStatus::Closed;
    }
    if (unanswered(soError)) {
      return ServiceConnectStatus::Timeout;
    }
    return ServiceConnectStatus::Error;
  }
  if (static_cast<uint32_t>(nowMs - startedMs_) >= timeoutMs_) {
    closeSocket();
    return ServiceConnectStatus::Timeout;
  }
  return ServiceConnectStatus::Pending;
}

void WifiTcpConnect::cancel() {
  closeSocket();
  held_ = ServiceConnectStatus::Pending;
}
