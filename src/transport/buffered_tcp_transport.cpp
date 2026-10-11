#include "transport/buffered_tcp_transport.hpp"
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
struct AcceptedSocket {
  int descriptor = -1;
  ~AcceptedSocket() { if (descriptor >= 0) close(descriptor); }
};
}
BufferedTcpTransport::BufferedTcpTransport(int listener, std::size_t capacity, std::string name, Starter starter)
    : listener_(listener), queue_(capacity), name_(std::move(name)), starter_(std::move(starter)) {
  if (name_.size() > 15) name_.resize(15);
}
BufferedTcpTransport::~BufferedTcpTransport() {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  if (!requestStop()) std::terminate();
  join();
  // Receiver callers use deferred cancellation; restore without adding a destructor cancellation point.
  pthread_setcancelstate(previousState, nullptr);
}
int BufferedTcpTransport::createThread(pthread_t *thread, void *(*routine)(void *), void *argument) {
  return pthread_create(thread, nullptr, routine, argument);
}
std::expected<void, int> BufferedTcpTransport::start() {
  std::lock_guard lock(lifecycle_);
  if (attempted_) return std::unexpected(EALREADY);
  attempted_ = true;
  if (stopping_) return std::unexpected(ECANCELED);
  int error = 0;
  const auto flags = fcntl(listener_, F_GETFL);
  if (flags < 0) error = errno;
  else if (!(flags & O_NONBLOCK)) error = EINVAL;
  int accepting = 0;
  socklen_t length = sizeof(accepting);
  if (!error && getsockopt(listener_, SOL_SOCKET, SO_ACCEPTCONN, &accepting, &length) < 0) error = errno;
  if (!error && !accepting) error = EINVAL;
  if (!error) {
    wake_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wake_ < 0) error = errno;
  }
  if (!error) error = starter_(&thread_, worker, this);
  if (error) {
    if (wake_ >= 0) { close(wake_); wake_ = -1; }
    queue_.fail(error);
    return std::unexpected(error);
  }
  started_ = true;
  return {};
}
std::expected<void, int> BufferedTcpTransport::requestStop() {
  std::lock_guard lock(lifecycle_);
  stopping_ = true;
  queue_.stop();
  if (wake_ >= 0) {
    const std::uint64_t signal = 1;
    ssize_t written;
    do { written = write(wake_, &signal, sizeof(signal)); } while (written < 0 && errno == EINTR);
    if (written < 0 && errno != EAGAIN) return std::unexpected(errno);
  }
  return {};
}
void BufferedTcpTransport::join() {
  int previousState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previousState);
  joinWorker();
  pthread_setcancelstate(previousState, nullptr);
}
void BufferedTcpTransport::joinWorker() {
  std::lock_guard joining(joining_);
  {
    std::lock_guard lock(lifecycle_);
    if (!started_ || joined_) return;
  }
  if (pthread_join(thread_, nullptr) != 0) std::terminate();
  std::lock_guard lock(lifecycle_);
  joined_ = true;
  close(wake_);
  wake_ = -1;
}
void *BufferedTcpTransport::worker(void *argument) {
  static_cast<BufferedTcpTransport *>(argument)->run();
  return nullptr;
}
bool BufferedTcpTransport::readable(int descriptor) {
  while (!stopping_) {
    std::array<pollfd,2> descriptors{{{wake_,POLLIN,0},{descriptor,POLLIN,0}}};
    const auto ready = poll(descriptors.data(), descriptors.size(), -1);
    if (ready < 0) {
      if (errno == EINTR) continue;
      queue_.fail(errno);
      return false;
    }
    if (stopping_) return false;
    if (descriptors[0].revents & (POLLERR | POLLNVAL | POLLHUP)) {
      queue_.fail(EBADF);
      return false;
    }
    if (descriptors[1].revents & POLLNVAL) { queue_.fail(EBADF); return false; }
    if (descriptors[1].revents & (POLLIN | POLLHUP | POLLERR)) return true;
  }
  return false;
}
void BufferedTcpTransport::run() {
  if (!name_.empty()) {
    const auto error = pthread_setname_np(pthread_self(), name_.c_str());
    if (error) { queue_.fail(error); return; }
  }
  AcceptedSocket socket;
  while (!stopping_ && socket.descriptor < 0) {
    if (!readable(listener_)) return;
    socket.descriptor = accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (socket.descriptor < 0 && errno != EINTR && errno != EAGAIN && errno != ECONNABORTED) {
      queue_.fail(errno);
      return;
    }
  }
  std::array<std::uint8_t,4096> bytes;
  while (!stopping_) {
    if (!readable(socket.descriptor)) return;
    const auto count = recv(socket.descriptor, bytes.data(), bytes.size(), 0);
    if (count == 0) { queue_.finish(); return; }
    if (count < 0) {
      if (errno == EINTR || errno == EAGAIN) continue;
      queue_.fail(errno);
      return;
    }
    const auto appended = queue_.append(std::span(bytes).first(count));
    if (appended.status != ByteQueueStatus::complete) return;
    if (appended.remaining > 16384) {
      pollfd wake{wake_,POLLIN,0};
      if (poll(&wake, 1, 10) < 0 && errno != EINTR) { queue_.fail(errno); return; }
    }
  }
}
