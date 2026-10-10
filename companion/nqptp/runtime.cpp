#include "runtime.hpp"
#include "debug.h"
#include "general-utilities.h"
#include "nqptp-clock-sources.h"
#include "nqptp-message-handlers.h"
#include "nqptp-ptp-definitions.h"
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netdb.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <system_error>

namespace nqptp {

extern int reset_clock_smoothing;
extern int clock_is_active;
extern uint64_t clock_validity_expiration_time;
uint64_t broadcasting_task(uint64_t call_time, void *private_data);
sockets_open_bundle *active_sockets = nullptr;
uint16_t active_general_port = 320;
static std::atomic_flag runtime_in_use = ATOMIC_FLAG_INIT;

static void fail(const std::string &operation, int error = errno) {
  throw std::system_error(error, std::generic_category(), operation);
}

Runtime::Runtime(Endpoints endpoints, MappingOperations mapping) : endpoints_(std::move(endpoints)) {
  if (runtime_in_use.test_and_set())
    fail("NQPTP runtime is already active in this process", EBUSY);
  state_owned_ = true;
  try {
    if (endpoints_.handle_signals) {
      sigset_t signals;
      sigemptyset(&signals);
      sigaddset(&signals, SIGINT);
      sigaddset(&signals, SIGTERM);
      const int error = pthread_sigmask(SIG_BLOCK, &signals, &previous_signal_mask_);
      if (error != 0) fail("block termination signals", error);
      signal_mask_changed_ = true;
      signal_fd_ = signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
      if (signal_fd_ < 0) fail("create termination signal descriptor");
    }
    bind_port(endpoints_.ptp_address, endpoints_.event_port);
    bind_port(endpoints_.ptp_address, endpoints_.general_port);
    bind_port(endpoints_.control_address, endpoints_.control_port);
    shared_memory_fd_ = shm_open(endpoints_.shared_memory_name.c_str(),
                                 O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (shared_memory_fd_ < 0)
      fail("create shared memory " + endpoints_.shared_memory_name +
           "; stop any existing NQPTP service and resolve a stale object before restarting");
    shared_memory_owned_ = true;
    if (fchmod(shared_memory_fd_, 0644) < 0) fail("make shared memory readable by the receiver");
    if (mapping.truncate(shared_memory_fd_, sizeof(shm_structure)) < 0)
      fail("size shared memory " + endpoints_.shared_memory_name);
    void *mapped = mapping.map(nullptr, sizeof(shm_structure), PROT_READ | PROT_WRITE,
                               MAP_SHARED | MAP_LOCKED, shared_memory_fd_, 0);
    if (mapped == MAP_FAILED) fail("map shared memory " + endpoints_.shared_memory_name);
    memory_ = static_cast<shm_structure *>(mapped);
    std::construct_at(memory_);
    memory_->version = NQPTP_SHM_STRUCTURES_VERSION;
    shared_memory = memory_;
    std::memset(clocks_private, 0, sizeof(clocks_private));
    reset_clock_smoothing = 0;
    clock_is_active = 0;
    clock_validity_expiration_time = 0;
    control_interface_name = endpoints_.shared_memory_name.c_str();
    active_general_port = endpoints_.general_port;
    active_sockets = &sockets_;
    next_broadcast_ = get_time_now() + 100000000;
    if (endpoints_.handle_signals) {
      sched_param priority{};
      priority.sched_priority = 5;
      const int error = pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);
      if (error) debug(1, "realtime scheduling unavailable: %s", strerror(error));
    }
  } catch (...) {
    release();
    throw;
  }
}

void Runtime::bind_port(const std::string &address, uint16_t &port) {
  if (address == "localhost") {
    bind_port("127.0.0.1", port);
    try {
      bind_port("::1", port);
    } catch (const std::system_error &error) {
      if (error.code().value() != EAFNOSUPPORT && error.code().value() != EPROTONOSUPPORT)
        throw;
    }
    return;
  }
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
  addrinfo *resolved = nullptr;
  const std::string service = std::to_string(port);
  const int error = getaddrinfo(address.empty() ? nullptr : address.c_str(), service.c_str(),
                               &hints, &resolved);
  if (error) throw std::runtime_error("resolve UDP endpoint: " + std::string(gai_strerror(error)));
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(resolved, freeaddrinfo);
  const auto first_socket = sockets_.sockets_open;
  for (auto *candidate = addresses.get(); candidate; candidate = candidate->ai_next) {
    if (candidate->ai_family != AF_INET && candidate->ai_family != AF_INET6) continue;
    const int fd = socket(candidate->ai_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_UDP);
    if (fd < 0) {
      if (errno == EAFNOSUPPORT || errno == EPROTONOSUPPORT) continue;
      fail("open UDP socket on port " + service);
    }
    if (sockets_.sockets_open >= MAX_OPEN_SOCKETS) {
      close(fd);
      fail("too many UDP endpoints", ENOSPC);
    }
    auto &entry = sockets_.sockets[sockets_.sockets_open++];
    entry = {fd, port, candidate->ai_family};
    if (candidate->ai_family == AF_INET6) {
      const int only_ipv6 = 1;
      if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &only_ipv6, sizeof(only_ipv6)) < 0)
        fail("restrict IPv6 UDP socket");
    }
    if (port != 0) {
      if (candidate->ai_family == AF_INET)
        reinterpret_cast<sockaddr_in *>(candidate->ai_addr)->sin_port = htons(port);
      else
        reinterpret_cast<sockaddr_in6 *>(candidate->ai_addr)->sin6_port = htons(port);
    }
    if (bind(fd, candidate->ai_addr, candidate->ai_addrlen) < 0)
      fail("bind required UDP port " + std::to_string(port) +
           "; stop any conflicting NQPTP or PTP service before activation");
    sockaddr_storage bound{};
    socklen_t size = sizeof(bound);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&bound), &size) < 0)
      fail("read bound UDP endpoint");
    port = candidate->ai_family == AF_INET
               ? ntohs(reinterpret_cast<sockaddr_in *>(&bound)->sin_port)
               : ntohs(reinterpret_cast<sockaddr_in6 *>(&bound)->sin6_port);
    entry.port = port;
  }
  if (sockets_.sockets_open == first_socket) fail("no supported UDP endpoint for port " + service, EAFNOSUPPORT);
}

Runtime::~Runtime() { release(); }

void Runtime::release() noexcept {
  if (!state_owned_) return;
  active_sockets = nullptr;
  control_interface_name = NQPTP_INTERFACE_NAME;
  shared_memory = nullptr;
  if (memory_) munmap(memory_, sizeof(shm_structure));
  memory_ = nullptr;
  if (shared_memory_fd_ >= 0) close(shared_memory_fd_);
  shared_memory_fd_ = -1;
  if (shared_memory_owned_) shm_unlink(endpoints_.shared_memory_name.c_str());
  shared_memory_owned_ = false;
  for (unsigned i = 0; i < sockets_.sockets_open; ++i) close(sockets_.sockets[i].number);
  sockets_.sockets_open = 0;
  if (signal_fd_ >= 0) close(signal_fd_);
  signal_fd_ = -1;
  if (signal_mask_changed_) pthread_sigmask(SIG_SETMASK, &previous_signal_mask_, nullptr);
  signal_mask_changed_ = false;
  state_owned_ = false;
  runtime_in_use.clear();
}

bool Runtime::poll_once(int timeout_ms) {
  std::array<pollfd, MAX_OPEN_SOCKETS + 1> watched{};
  unsigned count = sockets_.sockets_open;
  for (unsigned i = 0; i < count; ++i) watched[i] = {sockets_.sockets[i].number, POLLIN, 0};
  if (signal_fd_ >= 0) watched[count++] = {signal_fd_, POLLIN, 0};
  if (poll(watched.data(), count, timeout_ms) < 0) {
    if (errno == EINTR) return true;
    fail("poll companion sockets");
  }
  if (signal_fd_ >= 0 && watched[count - 1].revents & POLLIN) {
    signalfd_siginfo signal{};
    if (read(signal_fd_, &signal, sizeof(signal)) == sizeof(signal)) return false;
  }
  for (unsigned i = 0; i < sockets_.sockets_open; ++i) {
    if (!(watched[i].revents & POLLIN)) continue;
    std::array<char, 4096> bytes{};
    sockaddr_storage sender{};
    iovec payload{bytes.data(), bytes.size()};
    msghdr message{};
    message.msg_name = &sender;
    message.msg_namelen = sizeof(sender);
    message.msg_iov = &payload;
    message.msg_iovlen = 1;
    const ssize_t length = recvmsg(watched[i].fd, &message, MSG_DONTWAIT);
    const uint64_t time = get_time_now();
    if (length < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
      fail("receive companion datagram");
    }
    if (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) continue;
    const auto &endpoint = sockets_.sockets[i];
    if (endpoint.port == endpoints_.control_port) {
      handle_control_port_messages(bytes.data(), length, clocks_private, time);
      continue;
    }
    if (length < static_cast<ssize_t>(sizeof(ptp_common_message_header))) continue;
    ptp_common_message_header header;
    memcpy(&header, bytes.data(), sizeof(header));
    if ((header.reservedAndVersionPTP & 0xf) != 2 || ntohs(header.messageLength) != length) continue;
    uint16_t sender_port;
    const void *sender_address;
    if (sender.ss_family == AF_INET) {
      const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(&sender);
      sender_port = ntohs(ipv4->sin_port);
      sender_address = &ipv4->sin_addr;
    } else if (sender.ss_family == AF_INET6) {
      const auto *ipv6 = reinterpret_cast<const sockaddr_in6 *>(&sender);
      sender_port = ntohs(ipv6->sin6_port);
      sender_address = &ipv6->sin6_addr;
    } else continue;
    if (sender_port != endpoint.port) continue;
    char peer[INET6_ADDRSTRLEN]{};
    if (!inet_ntop(sender.ss_family, sender_address, peer, sizeof(peer))) continue;
    const int index = find_clock_source_record(peer, clocks_private);
    if (index < 0) continue;
    auto &clock = clocks_private[index];
    clock.time_of_last_use = time;
    switch (header.transportSpecificAndMessageID & 0xf) {
    case Announce: handle_announce(bytes.data(), length, &clock, time); break;
    case Sync: handle_sync(bytes.data(), length, &clock, time); break;
    case Follow_Up: handle_follow_up(bytes.data(), length, &clock, time); break;
    default: break;
    }
  }
  const uint64_t now = get_time_now();
  if (now >= next_broadcast_) next_broadcast_ = broadcasting_task(now, clocks_private);
  return true;
}

void Runtime::run(std::stop_token stop) {
  while (!stop.stop_requested() && poll_once()) {}
}

}
