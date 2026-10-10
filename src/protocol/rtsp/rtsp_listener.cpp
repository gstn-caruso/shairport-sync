#include "protocol/rtsp/rtsp_listener.hpp"
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <system_error>
#include <unistd.h>

RtspListener::RtspListener() : wake_(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)) {
  if (wake_ < 0)
    throw std::system_error(errno, std::generic_category(), "RTSP listener wake descriptor");
}

RtspListener::~RtspListener() {
  stop();
  releaseSockets();
  close(wake_);
}

void RtspListener::start(std::function<void(std::stop_token)> routine) {
  std::lock_guard lock(lifecycle_);
  if (worker_.joinable() || stop_.stop_requested())
    throw std::logic_error("RTSP listener already started or stopped");
  worker_ = std::jthread([this, routine = std::move(routine)] {
    routine(stop_.get_token());
    releaseSockets();
  });
}

void RtspListener::requestStop() {
  stop_.request_stop();
  uint64_t wake = 1;
  while (write(wake_, &wake, sizeof(wake)) < 0 && errno == EINTR) {}
}

void RtspListener::stop() {
  requestStop();
  std::lock_guard lock(lifecycle_);
  if (worker_.joinable())
    worker_.join();
}

bool RtspListener::addSocket(int descriptor) {
  if (fcntl(descriptor, F_SETFL, O_NONBLOCK) < 0) {
    close(descriptor);
    return false;
  }
  sockets_.push_back(descriptor);
  return true;
}

bool RtspListener::hasSockets() const { return !sockets_.empty(); }

void RtspListener::releaseSockets() {
  for (int descriptor : sockets_)
    close(descriptor);
  sockets_.clear();
}

int RtspListener::accept(std::stop_token stop, sockaddr *remote, socklen_t *size) {
  std::vector<pollfd> descriptors{{wake_, POLLIN, 0}};
  for (int descriptor : sockets_)
    descriptors.push_back({descriptor, POLLIN, 0});
  while (!stop.stop_requested()) {
    int ready = poll(descriptors.data(), descriptors.size(), 60000);
    if (stop.stop_requested())
      return -1;
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (ready == 0)
      return -1;
    for (auto &descriptor : descriptors) {
      if (descriptor.fd == wake_ || !(descriptor.revents & POLLIN))
        continue;
      int accepted = accept4(descriptor.fd, remote, size, SOCK_CLOEXEC);
      if (accepted < 0)
        continue;
      if (stop.stop_requested()) {
        close(accepted);
        return -1;
      }
      return accepted;
    }
  }
  return -1;
}
